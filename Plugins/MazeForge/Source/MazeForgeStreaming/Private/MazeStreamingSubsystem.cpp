#include "MazeStreamingSubsystem.h"

#include "Assets/MazeWorldManifest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MazeForgeCore.h"
#include "MazeRoomPool.h"
#include "MazeStreamingComponent.h"
#include "MazeStreamingRulesAsset.h"

static TAutoConsoleVariable<int32> CVarMazeDebugStreaming(
	TEXT("MazeForge.DebugStreaming"),
	0,
	TEXT("Show the state of the room load pool on screen."),
	ECVF_Cheat);

static void DumpStreamCsv(UWorld* World)
{
	if (!World)
	{
		return;
	}

	if (const UMazeStreamingSubsystem* Subsystem = World->GetSubsystem<UMazeStreamingSubsystem>())
	{
		if (const UMazeRoomPool* Pool = Subsystem->GetPool())
		{
			const FString Path = Pool->DumpSamplesToCsv();
			UE_LOG(LogMazeForge, Log, TEXT("Load profile saved: %s"), *Path);
		}
	}
}

static FAutoConsoleCommandWithWorld GMazeDumpStreamCsv(
	TEXT("MazeForge.DumpStreamCSV"),
	TEXT("Dump the room load profile to CSV."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&DumpStreamCsv));

void UMazeStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Pool = NewObject<UMazeRoomPool>(this, TEXT("MazeRoomPool"));
}

void UMazeStreamingSubsystem::Deinitialize()
{
	if (Pool)
	{
		Pool->Reset();
	}
	Super::Deinitialize();
}

bool UMazeStreamingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE only: in the editor world the designer is in charge of loading.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMazeStreamingSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMazeStreamingSubsystem, STATGROUP_Tickables);
}

void UMazeStreamingSubsystem::RegisterObserver(UMazeStreamingComponent* InObserver)
{
	Observer = InObserver;
	bCameraBoundsApplied = false;

	if (Pool)
	{
		Pool->Reset();
	}
}

void UMazeStreamingSubsystem::UnregisterObserver(UMazeStreamingComponent* InObserver)
{
	if (Observer.Get() == InObserver)
	{
		Observer.Reset();
	}
}

void UMazeStreamingSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UMazeStreamingComponent* Watcher = Observer.Get();
	UWorld* World = GetWorld();
	if (!Watcher || !World || !Pool)
	{
		return;
	}

	UMazeWorldManifest* Manifest = Watcher->Manifest.LoadSynchronous();
	UMazeStreamingRulesAsset* RulesAsset = Watcher->Rules.LoadSynchronous();

	if (!Manifest || !RulesAsset || Manifest->Rooms.Num() == 0)
	{
		// Three fatal conditions, and for a long time one silent return per frame between them.
		// "Nothing streams and nothing is said" is the worst shape a failure can take: there is
		// no symptom to search the log for, so the hunt starts at the wrong end every time.
		// Said once, and said specifically, because the fix is different for each.
		if (!bReportedIdleReason)
		{
			bReportedIdleReason = true;

			if (!Manifest)
			{
				UE_LOG(LogMazeForge, Error,
					TEXT("Streaming: %s has no manifest — the Manifest field is empty or points at ")
					TEXT("an asset that is gone (%s). Nothing will stream."),
					*GetNameSafe(Watcher->GetOwner()), *Watcher->Manifest.ToString());
			}
			else if (!RulesAsset)
			{
				UE_LOG(LogMazeForge, Error,
					TEXT("Streaming: %s has no streaming rules — the Rules field is empty or points ")
					TEXT("at an asset that is gone (%s). Nothing will stream."),
					*GetNameSafe(Watcher->GetOwner()), *Watcher->Rules.ToString());
			}
			else
			{
				UE_LOG(LogMazeForge, Error,
					TEXT("Streaming: manifest %s lists no rooms, so there is nothing to stream. ")
					TEXT("Nearly always this is a leftover from a build under a different Maze Name: ")
					TEXT("every maze writes its own manifest next to its own levels. Check the ")
					TEXT("Manifest field on %s."),
					*Manifest->GetPathName(), *GetNameSafe(Watcher->GetOwner()));
			}
		}

		return;
	}

	// Streaming works, so a break after this point is worth reporting again.
	bReportedIdleReason = false;

	// A different maze. This is the whole of switching: the pool is reset, which unloads every
	// room of the previous one, and the camera bounds are taken again from the new manifest.
	if (ActiveManifest.Get() != Manifest)
	{
		UE_LOG(LogMazeForge, Log, TEXT("Streaming: now reading manifest %s (was %s)."),
			*Manifest->GetPathName(),
			ActiveManifest.IsValid() ? *ActiveManifest->GetPathName() : TEXT("none"));

		ActiveManifest = Manifest;
		Pool->Reset();
		bCameraBoundsApplied = false;

		// Act this frame rather than at the next scheduled update: the player is already
		// standing in the new maze, and up to UpdateIntervalSeconds of empty world is exactly
		// the moment a transition must not have.
		TimeSinceUpdate = RulesAsset->UpdateIntervalSeconds;
	}

	if (!bCameraBoundsApplied)
	{
		// The camera does not appear on the first frame, so we try on the very first update.
		Watcher->ApplyCameraBounds(*Manifest);
		bCameraBoundsApplied = true;
	}

	// Recompute on a schedule rather than every frame: the rules walk over all rooms,
	// and decisions at frame rate are not needed anyway.
	TimeSinceUpdate += DeltaTime;
	if (TimeSinceUpdate < RulesAsset->UpdateIntervalSeconds)
	{
		return;
	}
	TimeSinceUpdate = 0.0f;

	const FMazeStreamQuery Query = Watcher->BuildQuery(Manifest);
	Pool->Update(Query, *Manifest, *RulesAsset, *World);
	Watcher->NotifyStreamingUpdated(*Pool, Query.CurrentRoomId);

	if (CVarMazeDebugStreaming.GetValueOnGameThread() != 0)
	{
		DrawDebug(*Manifest, Query);
	}
}

void UMazeStreamingSubsystem::DrawDebug(const UMazeWorldManifest& Manifest,
                                        const FMazeStreamQuery& Query) const
{
	if (!GEngine || !Pool)
	{
		return;
	}

	// Keys instead of -1: a message with a key is replaced rather than appended at the end.
	// With -1 and a lifetime of 0.2 s at a 10 Hz update, two copies of the list lived on
	// screen at once — an extra canvas every frame and twice as long an output.
	static constexpr int32 KeyBase = 0x4D5A0000;
	int32 Key = KeyBase;

	GEngine->AddOnScreenDebugMessage(Key++, 0.2f, FColor::White,
		FString::Printf(TEXT("MazeForge: loaded %d of %d, misses %d"),
			Pool->GetLoadedCount(), Manifest.Rooms.Num(), Pool->GetStreamingMissCount()));

	// The current room is the most important debug line. If it is empty, both the pinning
	// and the portal graph silently switch off, and the graph is the only rule that
	// guarantees the neighbours regardless of the room sizes.
	GEngine->AddOnScreenDebugMessage(Key++, 0.2f,
		Query.CurrentRoomId.IsNone() ? FColor::Red : FColor::White,
		FString::Printf(TEXT("  room %s | player X %.0f Z %.0f | camera X %.0f Z %.0f | frame %.0f x %.0f"),
			Query.CurrentRoomId.IsNone() ? TEXT("UNDETERMINED") : *Query.CurrentRoomId.ToString(),
			Query.PlayerLocation.X, Query.PlayerLocation.Z,
			Query.CameraLocation.X, Query.CameraLocation.Z,
			Query.CameraHalfExtentXZ.X * 2.0, Query.CameraHalfExtentXZ.Y * 2.0));

	int32 Shown = 0;
	for (const TPair<FName, FMazeRoomRuntime>& Pair : Pool->GetRooms())
	{
		// Rooms with zero desire used to be hidden — and those are exactly the ones people
		// ask "why is it not loading" about. We show everything up to the limit.
		if (Shown >= 16)
		{
			break;
		}

		const ULevelStreaming* Streaming = Pair.Value.Streaming.Get();

		// "Loaded" and "visible" are different things: a package can load and still not
		// enter the world. That is exactly what happened with the PIE prefix, so the
		// states are now told apart.
		const TCHAR* State = TEXT("—");
		FColor Color = FColor::Silver;

		if (!Streaming)
		{
			State = TEXT("NOT ATTACHED TO THE MAP");
			Color = FColor::Red;
		}
		else if (Streaming->IsLevelVisible())
		{
			State = TEXT("visible");
			Color = FColor::Green;
		}
		else if (Streaming->IsLevelLoaded())
		{
			State = TEXT("loaded, not visible");
			Color = FColor::Orange;
		}
		else if (Streaming->HasLoadRequestPending())
		{
			State = TEXT("loading");
			Color = FColor::Yellow;
		}

		GEngine->AddOnScreenDebugMessage(Key++, 0.2f, Color,
			FString::Printf(TEXT("  %s  desire %.2f  %s"),
				*Pair.Key.ToString(), Pair.Value.Desire, State));

		++Shown;
	}
}
