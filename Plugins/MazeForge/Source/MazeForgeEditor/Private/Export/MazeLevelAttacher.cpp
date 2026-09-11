#include "Export/MazeLevelAttacher.h"

#include "Assets/MazeBuildSettings.h"
#include "Assets/MazeGridAsset.h"
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
	UWorld* World = GetEditorWorld();
	if (!Asset || !World)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Attach: no asset or no open level."));
		return 0;
	}

	UMazeWorldManifest* Manifest = Asset->Manifest.LoadSynchronous();
	if (!Manifest || Manifest->Rooms.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Attach: the manifest is empty. Run Export Rooms To Levels first."));
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
	const FString LevelRoot = MazeExport::ScopedRoot(
		Settings ? Settings->LevelPackageRoot : TEXT("/Game/MazeForge/Maps"),
		Asset->GetSafeMazeName());

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
