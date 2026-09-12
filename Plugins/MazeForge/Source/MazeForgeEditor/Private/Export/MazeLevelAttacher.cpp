#include "Export/MazeLevelAttacher.h"

#include "Assets/MazeBuildSettings.h"
#include "Assets/MazeGridAsset.h"
#include "Assets/MazeWorldGraph.h"
#include "Assets/MazeWorldManifest.h"
#include "Editor.h"
#include "EditorLevelUtils.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EditorActorFolders.h"
#include "Export/MazeExportUtils.h"
#include "MazeForgeCore.h"

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

	const UMazeWorldManifest* Manifest = Asset->Manifest.LoadSynchronous();
	if (!Manifest || Manifest->Rooms.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Attach: the manifest of %s is empty. Run Export Rooms To Levels first."),
			*GetNameSafe(Asset));
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
