#include "MazeRoomPool.h"

#include "Assets/MazeWorldManifest.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "MazeForgeCore.h"
#include "MazeStreamingRulesAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Rules/MazeStreamingRule.h"

namespace
{
	/**
	 *  The package name without the PIE prefix.
	 *
	 *  When PIE starts, the editor duplicates the attached sublevels and renames them
	 *  to UEDPIE_0_L_R_000_000. The manifest holds the original name, so a direct
	 *  comparison found no entry at all, and the pool created its own — pointing at the
	 *  original packages. Those did load honestly (IsLevelLoaded returned true), but they
	 *  never entered the PIE world: the debug output reports "loaded" while the screen
	 *  stays empty.
	 */
	FName StripPiePrefix(const FName PackageName)
	{
		const FString AsString = PackageName.ToString();
		const FString Stripped = UWorld::RemovePIEPrefix(AsString);

		return Stripped.Equals(AsString, ESearchCase::CaseSensitive) ? PackageName : FName(*Stripped);
	}
}

void UMazeRoomPool::Reset()
{
	for (TPair<FName, FMazeRoomRuntime>& Pair : Rooms)
	{
		if (ULevelStreaming* Streaming = Pair.Value.Streaming.Get())
		{
			Streaming->SetShouldBeVisible(false);
			Streaming->SetShouldBeLoaded(false);
		}
	}

	Rooms.Reset();
	Samples.Reset();
	WarnedRooms.Reset();
	StreamingMisses = 0;

	// The next update starts from nothing loaded, so nothing it fails to have ready is its
	// fault. See the header.
	bFirstUpdateSinceReset = true;
}

int32 UMazeRoomPool::GetLoadedCount() const
{
	int32 Count = 0;
	for (const TPair<FName, FMazeRoomRuntime>& Pair : Rooms)
	{
		const ULevelStreaming* Streaming = Pair.Value.Streaming.Get();
		if (Streaming && Streaming->IsLevelLoaded())
		{
			++Count;
		}
	}
	return Count;
}

ULevelStreaming* UMazeRoomPool::ResolveStreamingLevel(UWorld& World, const FName PackageName)
{
	const FName Wanted = StripPiePrefix(PackageName);

	for (ULevelStreaming* Streaming : World.GetStreamingLevels())
	{
		if (!Streaming || StripPiePrefix(Streaming->GetWorldAssetPackageFName()) != Wanted)
		{
			continue;
		}

#if WITH_EDITORONLY_DATA
		// In PIE, UpdateTargetState refuses to load a sublevel whose eye is closed in the
		// Levels panel when "Only Load Visible Levels in PIE" is on. The refusal is
		// silent, and the designer hides rooms all the time — so we say it out loud.
		if (World.IsPlayInEditor() && !Streaming->GetShouldBeVisibleInEditor()
			&& !WarnedRooms.Contains(Wanted))
		{
			WarnedRooms.Add(Wanted);
			UE_LOG(LogMazeForge, Warning,
				TEXT("Streaming: room %s has its eye closed in the Levels panel. ")
				TEXT("It will not load in PIE while the eye is closed."),
				*Wanted.ToString());
		}
#endif

		return Streaming;
	}

	if (World.IsPlayInEditor())
	{
		// In PIE an entry of our own is useless: no copy of the package with the PIE
		// prefix exists, and the original will not enter the PIE world. Creating it
		// silently means getting exactly what we have already seen: "4 of 7 loaded" and
		// an empty scene. Better to say it out loud.
		if (!WarnedRooms.Contains(Wanted))
		{
			WarnedRooms.Add(Wanted);

			// Two entirely different faults arrive here looking identical, and for a long time
			// the message named only one of them. It sent the last person who hit this to press
			// Attach Rooms To Level — a button that was already correct, on a map that already
			// held its rooms — while the actual fault was a manifest belonging to another maze.
			// Whether the package exists at all is what separates the two.
			if (!FPackageName::DoesPackageExist(Wanted.ToString()))
			{
				UE_LOG(LogMazeForge, Error,
					TEXT("Streaming: room %s does not exist on disk. The manifest is asking for a ")
					TEXT("maze that was never built under this name — almost always it is the ")
					TEXT("manifest of a DIFFERENT maze. Check the Manifest field on the MazeForge ")
					TEXT("Streaming component; a maze with a Maze Name writes its own manifest ")
					TEXT("next to its own levels."),
					*Wanted.ToString());
			}
			else
			{
				UE_LOG(LogMazeForge, Error,
					TEXT("Streaming: room %s exists but is not attached to the persistent level. ")
					TEXT("Open the map, MazeForge mode, the Attach Rooms To Level button, ")
					TEXT("then save the map."),
					*Wanted.ToString());
			}

			// Printed once, on the first miss. Seeing what the map DOES hold, next to what was
			// asked for, is what turns a page of identical errors into one obvious answer.
			if (WarnedRooms.Num() == 1)
			{
				FString Attached;
				for (const ULevelStreaming* Level : World.GetStreamingLevels())
				{
					if (Level)
					{
						Attached += TEXT("\n    ");
						Attached += StripPiePrefix(Level->GetWorldAssetPackageFName()).ToString();
					}
				}

				UE_LOG(LogMazeForge, Error,
					TEXT("Streaming: the persistent level is holding these rooms:%s"),
					Attached.IsEmpty() ? TEXT("\n    (none)") : *Attached);
			}
		}
		return nullptr;
	}

	// In a packaged game there is no prefix and the entry can be created on the fly: the
	// pool works even without preparing the map by hand.
	ULevelStreamingDynamic* Created = NewObject<ULevelStreamingDynamic>(&World, NAME_None, RF_Transient);
	Created->SetWorldAssetByPackageName(Wanted);
	Created->LevelTransform = FTransform::Identity;
	Created->SetShouldBeLoaded(false);
	Created->SetShouldBeVisible(false);

	World.AddStreamingLevel(Created);
	return Created;
}

void UMazeRoomPool::Update(const FMazeStreamQuery& Query,
                           const UMazeWorldManifest& Manifest,
                           const UMazeStreamingRulesAsset& RulesAsset,
                           UWorld& World)
{
	const double Now = World.GetTimeSeconds();

	// --- 1. Desire from every rule.

	TMap<FName, float> Desire;
	for (const TObjectPtr<UMazeStreamingRule>& Rule : RulesAsset.Rules)
	{
		if (Rule && Rule->bEnabled)
		{
			Rule->Score(Query, Manifest, Desire);
		}
	}

	// --- 2. Synchronising the states with the manifest.

	for (const FMazeRoomEntry& Entry : Manifest.Rooms)
	{
		const FName PackageName(*Entry.Level.GetLongPackageName());

		FMazeRoomRuntime& State = Rooms.FindOrAdd(Entry.RoomId);

		// Belt as well as braces. Switching mazes resets the pool, which should make a stale
		// entry impossible; this is here because room ids collide between mazes and the
		// resulting failure is silent — the pool would keep streaming the old maze's rooms
		// under the new maze's names and report everything as fine.
		if (State.PackageName != PackageName)
		{
			if (ULevelStreaming* Stale = State.Streaming.Get())
			{
				Stale->SetShouldBeVisible(false);
				Stale->SetShouldBeLoaded(false);
			}

			State = FMazeRoomRuntime();
			State.PackageName = PackageName;
		}

		State.Desire = Desire.FindRef(Entry.RoomId);

		if (!State.Streaming.IsValid() && !PackageName.IsNone())
		{
			State.Streaming = ResolveStreamingLevel(World, PackageName);
		}

		// The room has just finished loading — record the time in the profile.
		ULevelStreaming* Streaming = State.Streaming.Get();
		if (Streaming && Streaming->IsLevelLoaded() && State.LoadedAtSeconds <= 0.0)
		{
			State.LoadedAtSeconds = Now;

			FMazeLoadSample Sample;
			Sample.RoomId = Entry.RoomId;
			Sample.Desire = State.Desire;
			Sample.RequestedAt = State.LoadStartedAtSeconds;
			Sample.CompletedAt = Now;
			Samples.Add(Sample);
		}
		else if (Streaming && !Streaming->IsLevelLoaded())
		{
			State.LoadedAtSeconds = 0.0;
		}
	}

	// --- 3. Sorting by desire.

	TArray<FName> Order;
	Rooms.GenerateKeyArray(Order);
	Order.Sort([this](const FName& A, const FName& B)
	{
		return Rooms[A].Desire > Rooms[B].Desire;
	});

	// --- 4. Decisions: the budget is handed out by priority, not by arrival order.
	//
	// The budget used to be computed once before the loop and was not freed when a room
	// was unloaded in the same pass. With the limit full this gave "first come, first
	// served": the rooms by the entry point took all the slots, while the room straight
	// ahead with a desire of 1.00 did not even get a load request. Now the order is
	// sorted by desire, and the budget goes to the rooms that need it most, evicting the
	// less needed ones.

	int32 PendingLoads = 0;
	int32 LoadedCount = 0;
	for (const TPair<FName, FMazeRoomRuntime>& Pair : Rooms)
	{
		const ULevelStreaming* Streaming = Pair.Value.Streaming.Get();
		if (!Streaming)
		{
			continue;
		}

		if (Streaming->HasLoadRequestPending())
		{
			++PendingLoads;
		}
		if (Streaming->IsLevelLoaded())
		{
			++LoadedCount;
		}
	}

	int32 Budget = FMath::Max(1, RulesAsset.MaxLoadedRooms);

	for (const FName RoomId : Order)
	{
		FMazeRoomRuntime& State = Rooms[RoomId];
		ULevelStreaming* Streaming = State.Streaming.Get();
		if (!Streaming)
		{
			continue;
		}

		const bool bLoaded = Streaming->IsLevelLoaded();
		const bool bPinned = (RoomId == Query.CurrentRoomId);

		// The engine queue priority comes straight from the desire.
		Streaming->SetPriority(FMath::RoundToInt(State.Desire * 1000.0f));

		bool bKeep;
		if (bPinned)
		{
			// The room under the player lives outside the budget and outside the thresholds.
			bKeep = true;
		}
		else if (Budget <= 0)
		{
			bKeep = false;
		}
		else
		{
			// Hysteresis: starting a load costs more than keeping something already loaded.
			bKeep = bLoaded
				? (State.Desire > RulesAsset.UnloadThreshold)
				: (State.Desire >= RulesAsset.LoadThreshold);
		}

		if (bKeep)
		{
			--Budget;
		}

		if (bKeep && !bLoaded && !State.bWantsLoad)
		{
			if (!bPinned && PendingLoads >= RulesAsset.MaxConcurrentLoads)
			{
				// The slot is taken. The room already holds its place in the budget, so
				// we start on the next update instead of handing it to a worse room.
				continue;
			}

			// A miss means the pool should have had this room ready and did not. On the first
			// update after a reset it could not have: it has only just been told which maze it
			// is looking at. Counting that printed a warning on every start of PIE and on every
			// door taken, which is how a line that should mean something becomes noise.
			if (bPinned && !bFirstUpdateSinceReset)
			{
				++StreamingMisses;
				UE_LOG(LogMazeForge, Warning,
					TEXT("Streaming: the player entered unloaded room %s."), *RoomId.ToString());
			}

			State.bWantsLoad = true;
			State.LoadStartedAtSeconds = Now;
			++PendingLoads;

			Streaming->SetShouldBeLoaded(true);
			Streaming->SetShouldBeVisible(true);
		}
		else if (!bKeep && State.bWantsLoad)
		{
			// We do not start an unload while a load is in flight.
			//
			// UWorld::AllowLevelLoadRequests returns false while there are levels pending
			// purge (World.cpp), and s.ForceGCAfterLevelStreamedOut by default orders a
			// full garbage collection after every unload. By unloading a room in the same
			// frame in which we requested a new one, we put the GC across the load with
			// our own hands — hence the hitch "while loading and unloading" at once.
			//
			// The margin of two rooms is needed so that under a continuous stream of loads
			// the pool does not grow without bound: once we exceed it, we unload
			// regardless of the queue.
			const bool bDeferUntilQuiet = PendingLoads > 0
				&& LoadedCount <= RulesAsset.MaxLoadedRooms + 2;

			if (bDeferUntilQuiet)
			{
				// The place is still occupied, so we honestly charge it to the budget.
				--Budget;
				continue;
			}

			// The minimum lifetime damps the flicker at the border, but it should hold a
			// place only for a room that is actually loaded. One that is not loaded and
			// whose desire has already dropped is released at once.
			const bool bOldEnough = State.LoadedAtSeconds <= 0.0
				|| (Now - State.LoadedAtSeconds) >= RulesAsset.MinTimeLoadedSeconds;

			if (bOldEnough)
			{
				State.bWantsLoad = false;
				Streaming->SetShouldBeLoaded(false);
				Streaming->SetShouldBeVisible(false);

				if (bLoaded)
				{
					--LoadedCount;
				}
			}
			else
			{
				// Not allowed to unload it yet — so the place is occupied after all.
				--Budget;
			}
		}
	}

	// Cleared at the end rather than the start, so the whole of this pass counts as the first
	// one: the rooms it asks for now are the ones the next pass is entitled to expect.
	bFirstUpdateSinceReset = false;
}

FString UMazeRoomPool::DumpSamplesToCsv() const
{
	FString Csv = TEXT("RoomId,Desire,RequestedAt,CompletedAt,LoadMs\n");
	for (const FMazeLoadSample& Sample : Samples)
	{
		Csv += FString::Printf(TEXT("%s,%.3f,%.3f,%.3f,%.2f\n"),
			*Sample.RoomId.ToString(), Sample.Desire,
			Sample.RequestedAt, Sample.CompletedAt, Sample.MillisecondsTaken());
	}

	const FString Path = FPaths::ProjectSavedDir() / TEXT("MazeForge")
		/ FString::Printf(TEXT("StreamProfile_%s.csv"), *FDateTime::Now().ToString());

	FFileHelper::SaveStringToFile(Csv, *Path);
	return Path;
}
