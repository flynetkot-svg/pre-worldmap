#include "Export/MazeDecorBinder.h"

#include "Actors/MazeKeepInPersistentLevel.h"
#include "Assets/MazeBuildSettings.h"
#include "Assets/MazeGridAsset.h"
#include "Assets/MazeWorldManifest.h"
#include "EditorLevelUtils.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Export/MazeExportUtils.h"
#include "Export/MazeLevelAttacher.h"
#include "Export/MazeRoomRetirement.h"
#include "FileHelpers.h"
#include "GameFramework/Info.h"
#include "HAL/PlatformTime.h"
#include "MazeForgeCore.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "MazeDecorBinder"

namespace
{
	using MazeExport::FMazeFolders;

	struct FRoomSlot
	{
		FName RoomId;
		FString PackageName;
		FString LevelAsset;
		FBox Bounds = FBox(ForceInit);
		ULevel* Level = nullptr;
	};

	ULevel* FindLoadedLevel(UWorld* World, const FString& PackageName)
	{
		const FName Wanted(*PackageName);
		for (ULevelStreaming* Streaming : World->GetStreamingLevels())
		{
			if (Streaming && Streaming->GetWorldAssetPackageFName() == Wanted)
			{
				return Streaming->GetLoadedLevel();
			}
		}
		return nullptr;
	}

	/**
	 *  The room a point stands in, compared in XZ. Half-open, so an actor on the line between
	 *  two rooms belongs to exactly one. Null outside every room — unlike the manifest's own
	 *  FindRoomAt, which answers with the nearest room because the player must always be in one.
	 *  A prop outside the maze is not in any room, and binding it to the nearest would be a guess.
	 */
	const FRoomSlot* RoomAt(const TArray<FRoomSlot>& Rooms, const FVector& Point)
	{
		for (const FRoomSlot& Room : Rooms)
		{
			const FBox& B = Room.Bounds;
			if (Point.X >= B.Min.X && Point.X < B.Max.X && Point.Z >= B.Min.Z && Point.Z < B.Max.Z)
			{
				return &Room;
			}
		}
		return nullptr;
	}

	/** Where an actor stands: the centre of its bounds, or its pivot when it has none (a light). */
	FVector StandPoint(const AActor* Actor)
	{
		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(/*bOnlyCollidingComponents*/ false, Origin, Extent,
			/*bIncludeFromChildActors*/ true);
		return Extent.IsNearlyZero() ? Actor->GetActorLocation() : Origin;
	}

	/** A hand-placed actor that is the top of its attachment chain — the unit that moves. */
	bool IsMovableRoot(const AActor* Actor, FName GeneratedTag)
	{
		return MazeExport::IsHandPlaced(Actor, GeneratedTag)
			&& !Actor->GetAttachParentActor()
			&& !Actor->IsChildActor();
	}

	FString JoinFolder(const FString& A, const FString& B)
	{
		if (A.IsEmpty()) { return B; }
		if (B.IsEmpty()) { return A; }
		return A / B;
	}

	/** The designer's own part of a folder path: what is left once the maze's arrangement is cut off. */
	FString DesignerPart(const FString& Path, const FMazeFolders& Folders)
	{
		if (Folders.FolderRoot.IsEmpty() || !Path.StartsWith(Folders.FolderRoot + TEXT("/")))
		{
			return Path;
		}

		FString Rest = Path.Mid(Folders.FolderRoot.Len() + 1);
		if (Folders.bFolderPerRoom)
		{
			int32 Slash = INDEX_NONE;
			Rest = Rest.FindChar(TEXT('/'), Slash) ? Rest.Mid(Slash + 1) : FString();
		}

		const FString& Sub = Folders.DecorSubFolder;
		if (!Sub.IsEmpty() && (Rest == Sub || Rest.StartsWith(Sub + TEXT("/"))))
		{
			Rest = Rest.Mid(FMath::Min(Rest.Len(), Sub.Len() + 1));
		}
		return Rest;
	}

	/** Where a moved actor goes in the Outliner. Room null means the persistent level. */
	FString DecorFolder(const FString& OldPath, const FRoomSlot* Room, const FMazeFolders& Folders)
	{
		const FString Own = DesignerPart(OldPath, Folders);
		if (!Room || Folders.FolderRoot.IsEmpty())
		{
			return Own;
		}
		return JoinFolder(JoinFolder(Folders.RoomFolder(Room->LevelAsset), Folders.DecorSubFolder), Own);
	}

	/** One planned move: where to, and which roots. */
	struct FMoveBatch
	{
		const FRoomSlot* Room = nullptr; // null: the persistent level
		TArray<AActor*> Roots;
	};
}

FString FMazeDecorBindReport::ToString() const
{
	return FString::Printf(
		TEXT("%d from the main level, %d between rooms, %d from abolished rooms (%d of them to the "
		     "main level); %d already home, %d kept where they are by class, tag or interface, %d outside "
		     "every room left alone, %d failed; %d package(s) saved in %.1f s"),
		FromMain, BetweenRooms, FromRetired, RetiredToMain, AlreadyHome, Kept,
		OutsideRooms, Failed, SavedPackages, Seconds);
}

FMazeDecorBindReport FMazeDecorBinder::Bind(const UMazeGridAsset* Asset)
{
	FMazeDecorBindReport Report;
	const double Start = FPlatformTime::Seconds();

	UWorld* World = MazeExport::EditorWorld();
	if (!Asset || !World)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Bind decor: no target asset or no open level."));
		return Report;
	}

	if (!MazeExport::EnsureOutputIsOurs(Asset, TEXT("Bind decor")))
	{
		return Report;
	}

	const UMazeWorldManifest* Manifest = FMazeRoomRetirement::ResolveManifest(Asset);
	if (!Manifest || Manifest->Rooms.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Bind decor: %s has no built rooms yet. Press Apply Changes first."), *Asset->GetName());
		return Report;
	}

	const FMazeFolders Folders(*Asset);
	const UMazeBuildSettings* Settings = Asset->BuildSettings.LoadSynchronous();

	TArray<UClass*> StayClasses;
	FName StayTag;
	if (Settings)
	{
		StayTag = Settings->DecorStayInMainTag;
		for (const TSoftClassPtr<AActor>& Class : Settings->DecorStayInMainClasses)
		{
			if (UClass* Loaded = Class.LoadSynchronous())
			{
				StayClasses.Add(Loaded);
			}
		}
	}

	auto StaysInMain = [&](const AActor* Actor)
	{
		if (Actor->IsA<AInfo>() || IMazeKeepInPersistentLevel::Wants(Actor)
			|| (!StayTag.IsNone() && Actor->Tags.Contains(StayTag)))
		{
			return true;
		}
		for (const UClass* Class : StayClasses)
		{
			if (Actor->IsA(Class))
			{
				return true;
			}
		}
		return false;
	};

	// ------------------------------------------------------------ 1. the rooms, on the map

	TArray<FRoomSlot> Rooms;
	Rooms.Reserve(Manifest->Rooms.Num());
	bool bAnyMissing = false;
	for (const FMazeRoomEntry& Entry : Manifest->Rooms)
	{
		FRoomSlot& Slot = Rooms.AddDefaulted_GetRef();
		Slot.RoomId = Entry.RoomId;
		Slot.PackageName = Entry.Level.GetLongPackageName();
		Slot.LevelAsset = FPackageName::GetShortName(Slot.PackageName);
		Slot.Bounds = Entry.WorldBounds;
		bAnyMissing |= !FindLoadedLevel(World, Slot.PackageName);
	}

	// A move needs the level loaded in this world. Attaching only adds what is missing.
	if (bAnyMissing)
	{
		FMazeLevelAttacher::AttachRooms(const_cast<UMazeGridAsset*>(Asset));
	}

	int32 Shown = 0;
	for (FRoomSlot& Slot : Rooms)
	{
		Slot.Level = FindLoadedLevel(World, Slot.PackageName);

		// The editor will not paste into a hidden level, and decor arriving in one would
		// look as if it had vanished.
		if (Slot.Level && !Slot.Level->bIsVisible)
		{
			UEditorLevelUtils::SetLevelVisibility(Slot.Level, true, false);
			++Shown;
		}
	}

	// ------------------------------------------------------------ 2. what goes where

	TMap<const FRoomSlot*, FMoveBatch> Batches;
	auto Plan = [&Batches](const FRoomSlot* To, AActor* Root)
	{
		FMoveBatch& Batch = Batches.FindOrAdd(To);
		Batch.Room = To;
		Batch.Roots.Add(Root);
	};

	// a. The persistent level: everything standing in a room, bar what is kept there.
	for (AActor* Actor : World->PersistentLevel->Actors)
	{
		if (!IsMovableRoot(Actor, Folders.GeneratedTag))
		{
			continue;
		}

		const FRoomSlot* Room = RoomAt(Rooms, StandPoint(Actor));
		if (!Room)
		{
			continue; // outside the maze: not ours to judge, and not counted
		}

		if (StaysInMain(Actor))
		{
			++Report.Kept;
		}
		else if (!Room->Level)
		{
			++Report.Failed;
		}
		else
		{
			Plan(Room, Actor);
			++Report.FromMain;
		}
	}

	// b. Room levels: decor standing in another room since the slicing changed.
	for (const FRoomSlot& Slot : Rooms)
	{
		if (!Slot.Level)
		{
			continue;
		}

		for (AActor* Actor : Slot.Level->Actors)
		{
			if (!IsMovableRoot(Actor, Folders.GeneratedTag))
			{
				continue;
			}

			// Asked to stay out of rooms, yet in one: put there by hand, and left as it is.
			if (StaysInMain(Actor))
			{
				++Report.Kept;
				continue;
			}

			const FRoomSlot* Room = RoomAt(Rooms, StandPoint(Actor));
			if (Room == &Slot)
			{
				++Report.AlreadyHome;
			}
			else if (!Room)
			{
				++Report.OutsideRooms;
			}
			else if (!Room->Level)
			{
				++Report.Failed;
			}
			else
			{
				Plan(Room, Actor);
				++Report.BetweenRooms;
			}
		}
	}

	// c. Levels of abolished rooms. Apply took them off the map; they are opened again only to
	//    be emptied, and taken off once more at the end.
	TArray<ULevel*> Opened;
	for (const FString& PackageName : FMazeRoomRetirement::FindStaleLevels(Asset))
	{
		ULevel* Level = FindLoadedLevel(World, PackageName);
		if (!Level)
		{
			ULevelStreaming* Streaming = UEditorLevelUtils::AddLevelToWorld(
				World, *PackageName, ULevelStreamingDynamic::StaticClass());
			Level = Streaming ? Streaming->GetLoadedLevel() : nullptr;
			if (!Level)
			{
				continue;
			}
			Opened.Add(Level);
		}

		for (AActor* Actor : Level->Actors)
		{
			if (!IsMovableRoot(Actor, Folders.GeneratedTag))
			{
				continue;
			}

			// What stays out of rooms goes to the persistent level: the abolished one closes again.
			const FRoomSlot* Room = StaysInMain(Actor) ? nullptr : RoomAt(Rooms, StandPoint(Actor));
			if (Room && !Room->Level)
			{
				++Report.Failed;
				continue;
			}

			Plan(Room && Room->Level ? Room : nullptr, Actor);
			++Report.FromRetired;
			Report.RetiredToMain += Room ? 0 : 1;
		}
	}

	auto CloseOpened = [&Opened]()
	{
		for (ULevel* Level : Opened)
		{
			UEditorLevelUtils::RemoveLevelFromWorld(Level);
		}
		Opened.Reset();
	};

	if (Report.Planned() == 0)
	{
		CloseOpened();
		Report.Seconds = FPlatformTime::Seconds() - Start;
		UE_LOG(LogMazeForge, Log, TEXT("Bind decor: nothing to move — %s."), *Report.ToString());
		return Report;
	}

	// ------------------------------------------------------------ 3. ask, once

	const FText Question = FText::Format(
		LOCTEXT("BindAsk",
			"Move {0} hand-placed actor(s) into the levels of the rooms they stand in?\n\n"
			"From the main level: {1}\nBetween rooms: {2}\nFrom abolished rooms: {3} "
			"({4} of them stand in no room and go to the main level)\n\n"
			"Every level touched is saved afterwards, the main level included. References between "
			"actors in different levels do not survive the move."),
		Report.Planned(), Report.FromMain, Report.BetweenRooms, Report.FromRetired,
		Report.RetiredToMain);

	if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
	{
		CloseOpened();
		Report.bCancelled = true;
		UE_LOG(LogMazeForge, Log, TEXT("Bind decor: cancelled, nothing moved."));
		return Report;
	}

	// ------------------------------------------------------------ 4. move

	TSet<UPackage*> Touched;
	Touched.Add(World->GetOutermost());

	{
		FScopedSlowTask Progress(static_cast<float>(Batches.Num()), LOCTEXT("BindProgress", "Binding decor to rooms..."));
		Progress.MakeDialog();

		for (TPair<const FRoomSlot*, FMoveBatch>& Pair : Batches)
		{
			Progress.EnterProgressFrame(1.0f);

			const FMoveBatch& Batch = Pair.Value;
			ULevel* Dest = Batch.Room ? Batch.Room->Level : World->PersistentLevel.Get();

			// The whole attachment tree moves together, or the children are left behind.
			TArray<AActor*> ToMove;
			for (AActor* Root : Batch.Roots)
			{
				ToMove.AddUnique(Root);
				TArray<AActor*> Children;
				Root->GetAttachedActors(Children, true, /*bRecursivelyIncludeAttachedActors*/ true);
				for (AActor* Child : Children)
				{
					ToMove.AddUnique(Child);
				}
			}

			for (const AActor* Actor : ToMove)
			{
				Touched.Add(Actor->GetLevel()->GetOutermost());
			}
			Touched.Add(Dest->GetOutermost());

			TArray<AActor*> Arrived;
			const int32 Moved = UEditorLevelUtils::MoveActorsToLevel(ToMove, Dest,
				/*bWarnAboutReferences*/ false, /*bWarnAboutRenaming*/ false,
				/*bMoveAllOrFail*/ false, &Arrived);
			Report.Failed += FMath::Max(0, ToMove.Num() - Moved);

			for (AActor* Actor : Arrived)
			{
				if (Actor && !Actor->GetAttachParentActor())
				{
					Actor->SetFolderPath(FName(*DecorFolder(Actor->GetFolderPath().ToString(),
						Batch.Room, Folders)));
				}
			}
		}
	}

	// ------------------------------------------------------------ 5. save, then close

	// Saved before the abolished levels are taken off again: once off the map they may be
	// unloaded, and an unsaved emptying would be lost while the decor sits in its new room —
	// on disk, twice.
	TArray<UPackage*> ToSave = Touched.Array();
	UEditorLoadingAndSavingUtils::SavePackages(ToSave, /*bOnlyDirty*/ true);
	Report.SavedPackages = ToSave.Num();

	if (Opened.Num() > 0)
	{
		CloseOpened();
		UEditorLoadingAndSavingUtils::SavePackages({ World->GetOutermost() }, true);
	}

	Report.Seconds = FPlatformTime::Seconds() - Start;

	UE_LOG(LogMazeForge, Log, TEXT("Bind decor: %s.%s"), *Report.ToString(),
		Shown > 0 ? *FString::Printf(TEXT(" %d hidden room level(s) were shown to receive it."), Shown)
		          : TEXT(""));

	if (Report.Failed > 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Bind decor: %d actor(s) could not be moved and stayed where they were — a room "
			     "level that is locked or not on the map. Unlock it in the Levels panel and press "
			     "Bind again."), Report.Failed);
	}

	return Report;
}

#undef LOCTEXT_NAMESPACE
