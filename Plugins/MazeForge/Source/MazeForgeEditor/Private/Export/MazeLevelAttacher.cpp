#include "Export/MazeLevelAttacher.h"

#include "Assets/MazeBuildSettings.h"
#include "Assets/MazeGridAsset.h"
#include "Assets/MazeWorldGraph.h"
#include "Assets/MazeWorldManifest.h"
#include "Data/MazeRoomDesc.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EditorActorFolders.h"
#include "Export/MazeExportUtils.h"
#include "MazeForgeCore.h"
#include "Misc/PackageName.h"

namespace
{
	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	ULevelStreaming* FindStreamingLevel(UWorld* World, FName PackageName)
	{
		for (ULevelStreaming* Streaming : World->GetStreamingLevels())
		{
			if (Streaming && Streaming->GetWorldAssetPackageFName() == PackageName)
			{
				return Streaming;
			}
		}
		return nullptr;
	}
}

int32 FMazeLevelAttacher::AttachRooms(UMazeGridAsset* Asset)
{
	if (!Asset)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Attach: no asset."));
		return 0;
	}

	UMazeWorldManifest* Manifest = Asset->Manifest.LoadSynchronous();

	// No pointer is not the same as no manifest. The pointer is written onto the grid asset by
	// the export and survives only if the grid asset is saved afterwards; an editor that dies
	// in between — which is exactly what a big first build did — leaves 124 built levels and
	// a complete manifest on disk, and a grid asset that no longer knows where they are. So
	// the manifest is looked for where the export puts it, by the same rule, before giving up.
	if (!Manifest)
	{
		const UMazeBuildSettings* Settings = Asset->BuildSettings.LoadSynchronous();
		const FString MazeName = Asset->GetSafeMazeName();
		const FString PackageName = MazeExport::ManifestPackageName(Settings, MazeName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"),
			*PackageName, *MazeExport::ManifestAssetName(Settings, MazeName));

		if (FPackageName::DoesPackageExist(PackageName))
		{
			Manifest = LoadObject<UMazeWorldManifest>(nullptr, *ObjectPath);
		}

		if (Manifest)
		{
			// Put the link back, so the next thing that asks does not have to search. Marked
			// dirty rather than saved: saving somebody's asset behind their back is not this
			// function's call, and Ctrl+Shift+S is one keystroke away.
			Asset->Modify();
			Asset->Manifest = Manifest;

			UE_LOG(LogMazeForge, Warning,
				TEXT("Attach: %s had lost its link to its manifest — it was not saved after the "
				     "last build. Found %s where the build puts it and linked it again. Save %s "
				     "to keep the link."),
				*Asset->GetName(), *PackageName, *Asset->GetName());
		}
	}

	// Two different messages for two different states. "Empty" used to cover both, and it was
	// wrong for the one that actually happened: the manifest was there and full.
	if (!Manifest)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Attach: %s has no manifest — it has never been built, or its manifest was "
			     "moved. Press Apply Changes."), *GetNameSafe(Asset));
		return 0;
	}

	if (Manifest->Rooms.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Attach: manifest %s lists no rooms. Press Apply Changes."),
			*Manifest->GetName());
		return 0;
	}

	return AttachRooms(Manifest);
}

int32 FMazeLevelAttacher::AttachRooms(const UMazeWorldManifest* Manifest)
{
	UWorld* World = GetEditorWorld();
	if (!Manifest || !World)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Attach: no manifest or no open level."));
		return 0;
	}

	int32 Added = 0;
	int32 Skipped = 0;

	for (const FMazeRoomEntry& Room : Manifest->Rooms)
	{
		const FString PackageName = Room.Level.GetLongPackageName();
		if (PackageName.IsEmpty())
		{
			continue;
		}

		if (FindStreamingLevel(World, FName(*PackageName)))
		{
			++Skipped;
			continue;
		}

		ULevelStreaming* Streaming = UEditorLevelUtils::AddLevelToWorld(
			World, *PackageName, ULevelStreamingDynamic::StaticClass());

		if (!Streaming)
		{
			continue;
		}

		// In the editor the room is visible — the designer works in the context of its
		// neighbours. In the game the pool decides on loading, so it does not load by itself.
		Streaming->SetShouldBeVisibleInEditor(true);
		Streaming->SetShouldBeLoaded(false);
		Streaming->SetShouldBeVisible(false);

		++Added;
	}

	World->MarkPackageDirty();

	UE_LOG(LogMazeForge, Log,
		TEXT("Attach rooms: %d added, %d already present. Save the persistent level."),
		Added, Skipped);

	return Added;
}

int32 FMazeLevelAttacher::AttachWorld(const UMazeWorldGraph* Graph)
{
	if (!Graph)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Attach world: no world graph. Pick one in the World Graph field."));
		return 0;
	}

	if (Graph->Mazes.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Attach world: %s names no mazes. Add them in the world map window."),
			*Graph->GetName());
		return 0;
	}

	int32 Added = 0;
	int32 Missing = 0;

	for (const TSoftObjectPtr<UMazeWorldManifest>& Entry : Graph->Mazes)
	{
		// Loaded one at a time and on purpose. A manifest is the list of a maze's rooms and
		// nothing else — no cells, no meshes — so ten of them cost almost nothing, which is the
		// whole reason the world map can draw a world without opening it.
		const UMazeWorldManifest* Manifest = Entry.LoadSynchronous();

		if (!Manifest)
		{
			++Missing;
			UE_LOG(LogMazeForge, Warning,
				TEXT("Attach world: %s is in the graph but could not be loaded. It was probably "
				     "renamed or deleted; remove it from the graph or build it again."),
				*Entry.ToString());
			continue;
		}

		Added += AttachRooms(Manifest);
	}

	UE_LOG(LogMazeForge, Log,
		TEXT("Attach world: %d mazes in %s, %d levels added, %d manifests missing. "
		     "Save the persistent level — Ctrl+Shift+S."),
		Graph->Mazes.Num(), *Graph->GetName(), Added, Missing);

	return Added;
}

int32 FMazeLevelAttacher::RemoveOutlinerFolders(UWorld* World, const UMazeGridAsset* Asset)
{
	if (!World || !Asset)
	{
		return 0;
	}

	const UMazeBuildSettings* Settings = Asset->BuildSettings.LoadSynchronous();
	const FString MazeName = Asset->GetSafeMazeName();

	const FString FolderRoot = MazeExport::OutlinerRoot(
		Settings ? Settings->OutlinerRootFolder : TEXT("Levels"), MazeName);
	const FString MeshSubFolder = MazeExport::CleanFolder(
		Settings ? Settings->OutlinerMeshSubFolder : TEXT("MazeMeshes"));
	const bool bFolderPerRoom = Settings ? Settings->bOutlinerFolderPerRoom : true;

	if (FolderRoot.IsEmpty())
	{
		return 0;
	}

	FActorFolders& Folders = FActorFolders::Get();
	int32 Removed = 0;

	auto Delete = [&Folders, World, &Removed](const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return;
		}

		// The invalid root object is the ordinary world root — what SetFolderPath uses for an
		// actor in a plain, non-partitioned level, which is every actor the export places.
		Folders.DeleteFolder(*World, FFolder(FFolder::GetInvalidRootObject(), FName(*Path)));
		++Removed;
	};

	// Deepest first. Deleting a parent before its children is asking the folder container to
	// decide what happens to them, and there is no version of that answer worth relying on.
	if (bFolderPerRoom)
	{
		for (const FMazeRoomDesc& Room : Asset->Rooms)
		{
			const FString RoomFolder = FolderRoot / MazeExport::LevelAssetName(MazeName, Room.RoomId);

			if (!MeshSubFolder.IsEmpty())
			{
				Delete(RoomFolder / MeshSubFolder);
			}

			Delete(RoomFolder);
		}
	}

	// The maze's own compartment goes too, but never the shared root above it.
	if (!MazeName.IsEmpty())
	{
		Delete(FolderRoot);
	}

	return Removed;
}

int32 FMazeLevelAttacher::DetachRooms(UMazeGridAsset* Asset)
{
	UWorld* World = GetEditorWorld();
	if (!Asset || !World)
	{
		return 0;
	}

	const UMazeBuildSettings* Settings = Asset->BuildSettings.LoadSynchronous();

	// Scoped, so detaching one maze cannot take another maze's levels out of the map with it.
	// Matching on the path and not on the asset name is deliberate: the compartment is a folder,
	// there is nothing to parse, and there is no way to get the parsing wrong.
	//
	// The trailing slash is the whole of that promise, and it was missing. "/Maps/TEST" is a
	// prefix of "/Maps/TEST2/L_TEST2_R_000_000", so switching away from TEST quietly took TEST2
	// off the map as well — and TEST20, and TEST_OLD. The map was then saved in that state, and
	// the next session opened a world with half its levels gone and nothing said why.
	// A folder boundary is a slash; comparing without it compares letters, not folders.
	const FString LevelRoot = MazeExport::ScopedRoot(
		Settings ? Settings->LevelPackageRoot : TEXT("/Game/MazeForge/Maps"),
		Asset->GetSafeMazeName()) + TEXT("/");

	TArray<ULevelStreaming*> ToRemove;
	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (Streaming && Streaming->GetWorldAssetPackageFName().ToString().StartsWith(LevelRoot))
		{
			ToRemove.Add(Streaming);
		}
	}

	int32 Removed = 0;
	for (ULevelStreaming* Streaming : ToRemove)
	{
		// A loaded level is taken out the regular way, an unloaded one just has its entry
		// dropped: RemoveLevelFromWorld only works with an actually loaded ULevel.
		if (ULevel* Loaded = Streaming->GetLoadedLevel())
		{
			Removed += UEditorLevelUtils::RemoveLevelFromWorld(Loaded) ? 1 : 0;
		}
		else
		{
			Removed += World->RemoveStreamingLevel(Streaming) ? 1 : 0;
		}
	}

	// The levels are gone; their Outliner folders are not. An empty folder tree left behind is
	// not cosmetic — after switching mazes it is a list of rooms that no longer exist anywhere,
	// sitting above the rooms that do.
	const int32 FoldersRemoved = RemoveOutlinerFolders(World, Asset);

	World->MarkPackageDirty();

	UE_LOG(LogMazeForge, Log,
		TEXT("Detach rooms: %d removed, %d Outliner folders cleared. Save the persistent level."),
		Removed, FoldersRemoved);

	return Removed;
}

// ---------------------------------------------------- pointing the editor at a room

namespace
{
	/** Every streaming level of this maze that is in the open map, by manifest. */
	void GatherRoomStreamingLevels(const UMazeGridAsset* Asset,
	                               TArray<ULevelStreaming*>& OutLevels)
	{
		UWorld* World = GetEditorWorld();
		if (!Asset || !World)
		{
			return;
		}

		const UMazeWorldManifest* Manifest = Asset->Manifest.LoadSynchronous();
		if (!Manifest)
		{
			return;
		}

		for (const FMazeRoomEntry& Entry : Manifest->Rooms)
		{
			const FString PackageName = Entry.Level.GetLongPackageName();
			if (PackageName.IsEmpty())
			{
				continue;
			}

			if (ULevelStreaming* Streaming = FindStreamingLevel(World, FName(*PackageName)))
			{
				OutLevels.Add(Streaming);
			}
		}
	}
}

bool FMazeLevelAttacher::AreRoomLevelsVisible(const UMazeGridAsset* Asset)
{
	TArray<ULevelStreaming*> Levels;
	GatherRoomStreamingLevels(Asset, Levels);

	for (const ULevelStreaming* Streaming : Levels)
	{
		if (Streaming->GetShouldBeVisibleInEditor())
		{
			return true;
		}
	}

	return false;
}

int32 FMazeLevelAttacher::SetRoomLevelsVisible(const UMazeGridAsset* Asset, bool bVisible)
{
	TArray<ULevelStreaming*> Levels;
	GatherRoomStreamingLevels(Asset, Levels);

	if (Levels.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Room levels: none of this maze's levels are in the open map. Press Attach "
			     "Rooms To Level, or build it first."));
		return 0;
	}

	int32 Changed = 0;
	for (ULevelStreaming* Streaming : Levels)
	{
		if (Streaming->GetShouldBeVisibleInEditor() == bVisible)
		{
			continue;
		}

		// Through EditorLevelUtils and not by setting the flag: this is the call the eye icon
		// in the Levels panel makes, and it is what rebuilds the render state. Flipping the
		// property alone leaves the viewport showing a level the world believes is hidden.
		if (ULevel* Loaded = Streaming->GetLoadedLevel())
		{
			UEditorLevelUtils::SetLevelVisibility(Loaded, bVisible, false);
			++Changed;
		}
	}

	UE_LOG(LogMazeForge, Log,
		TEXT("Room levels: %d of %d now %s. The mode draws the preview for whatever is hidden, "
		     "so the slicing is visible on the drawing itself."),
		Changed, Levels.Num(), bVisible ? TEXT("shown") : TEXT("hidden"));

	return Changed;
}

FString FMazeLevelAttacher::MakeRoomLevelCurrent(const UMazeGridAsset* Asset,
                                                 const FIntPoint& CellXZ)
{
	if (!Asset)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Room level: no Target Asset set."));
		return FString();
	}

	if (Asset->Rooms.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Room level: %s has not been sliced into rooms, so there are no room levels "
			     "to point at. Press Apply Changes."), *Asset->GetName());
		return FString();
	}

	const FMazeRoomDesc* Room = MazeRooms::FindAtXZ(Asset->Rooms, CellXZ);
	if (!Room)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Room level: cell X %d Z %d is outside every room of %s."),
			CellXZ.X, CellXZ.Y, *Asset->GetName());
		return FString();
	}

	// Through the manifest and not through the build settings, deliberately. The manifest is
	// what the last export actually wrote; the settings are what the next one would write.
	// Between a change of Maze Name and the Apply Changes that acts on it the two disagree,
	// and only one of them names a level that exists.
	const UMazeWorldManifest* Manifest = Asset->Manifest.LoadSynchronous();
	if (!Manifest)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Room level: %s has never been built, so its rooms have no levels yet. Press "
			     "Apply Changes."), *Asset->GetName());
		return FString();
	}

	const FMazeRoomEntry* Entry = Manifest->FindRoom(Room->RoomId);
	if (!Entry)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Room level: room %s is in the slicing but not in manifest %s. The maze has "
			     "been re-sliced since it was last built — press Apply Changes."),
			*Room->RoomId.ToString(), *Manifest->GetName());
		return FString();
	}

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return FString();
	}

	const FString PackageName = Entry->Level.GetLongPackageName();
	ULevelStreaming* Streaming = PackageName.IsEmpty()
		? nullptr
		: FindStreamingLevel(World, FName(*PackageName));

	if (!Streaming)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Room level: room %s is not attached to this map, so the editor cannot put "
			     "anything into it. Press Attach Rooms To Level."), *Room->RoomId.ToString());
		return FString();
	}

	// Locked levels are left alone rather than forced. A lock is somebody saying "not this
	// one", and quietly overriding it would make the next surprise theirs, not ours.
	if (Streaming->bLocked)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Room level: the level of room %s is locked in the Levels panel. Unlock it "
			     "there first."), *Room->RoomId.ToString());
		return FString();
	}

	UEditorLevelUtils::MakeLevelCurrent(Streaming);

	const FString LevelName = FPackageName::GetShortName(PackageName);

	UE_LOG(LogMazeForge, Log,
		TEXT("Room level: new actors now go into %s — room %s. Drag your decor in; the export "
		     "leaves anything it did not create alone."),
		*LevelName, *Room->RoomId.ToString());

	return LevelName;
}

FString FMazeLevelAttacher::GetCurrentLevelName()
{
	const UWorld* World = GetEditorWorld();
	const ULevel* Current = World ? World->GetCurrentLevel() : nullptr;

	if (!Current)
	{
		return FString();
	}

	return FPackageName::GetShortName(Current->GetOutermost()->GetName());
}

bool FMazeLevelAttacher::IsPersistentLevelCurrent()
{
	const UWorld* World = GetEditorWorld();

	// No world counts as persistent: the answer feeds a warning, and a warning that goes quiet
	// when the question cannot be answered is the kind that is missing exactly when it matters.
	return !World || World->GetCurrentLevel() == World->PersistentLevel;
}
