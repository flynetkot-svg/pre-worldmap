#include "Export/MazeRoomRetirement.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Assets/MazeBuildSettings.h"
#include "Assets/MazeGridAsset.h"
#include "Assets/MazeWorldManifest.h"
#include "Data/MazeRoomDesc.h"
#include "Editor.h"
#include "EditorActorFolders.h"
#include "EditorLevelUtils.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Export/MazeExportUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "MazeForgeCore.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Spawner/MazeObjectIdComponent.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/** Where retired assets go. A sibling of the live assets, not their parent. */
	const TCHAR* DeprecatedFolder = TEXT("Deprecated");

	ULevelStreaming* FindStreamingLevelByPath(UWorld* World, const FString& PackageName)
	{
		if (!World)
		{
			return nullptr;
		}

		const FName Wanted(*PackageName);
		for (ULevelStreaming* Streaming : World->GetStreamingLevels())
		{
			if (Streaming && Streaming->GetWorldAssetPackageFName() == Wanted)
			{
				return Streaming;
			}
		}

		return nullptr;
	}

	/**
	 *  Whether this level holds anything the export did not create.
	 *
	 *  The export tags everything it spawns with a UMazeObjectIdComponent, so an actor without
	 *  one was put there by a hand. Worth knowing before the level goes anywhere: the actors go
	 *  with it, and the designer is owed the news that his decor has just left the map.
	 */
	bool HoldsHandPlacedActors(const ULevel* Level)
	{
		if (!Level)
		{
			return false;
		}

		for (const AActor* Actor : Level->Actors)
		{
			if (!Actor || Actor->IsA<AWorldSettings>())
			{
				continue;
			}

			if (!Actor->FindComponentByClass<UMazeObjectIdComponent>())
			{
				return true;
			}
		}

		return false;
	}

	/** Everything the move needs to know about one maze, worked out once. */
	struct FMazeFolders
	{
		FString MazeName;
		FString LevelRoot;
		FString MeshRoot;
		FString FolderRoot;
		FString MeshSubFolder;
		bool bFolderPerRoom = true;
		bool bSplitByBand = true;
		int32 PerFlush = 16;

		explicit FMazeFolders(const UMazeGridAsset& Asset)
		{
			const UMazeBuildSettings* Settings = Asset.BuildSettings.LoadSynchronous();
			MazeName = Asset.GetSafeMazeName();

			LevelRoot = MazeExport::ScopedRoot(
				Settings ? Settings->LevelPackageRoot : FString(TEXT("/Game/MazeForge/Maps")),
				MazeName);
			MeshRoot = MazeExport::ScopedRoot(
				Settings ? Settings->MeshPackageRoot : FString(TEXT("/Game/MazeForge/Meshes")),
				MazeName);

			FolderRoot = MazeExport::OutlinerRoot(
				Settings ? Settings->OutlinerRootFolder : TEXT("Levels"), MazeName);
			MeshSubFolder = MazeExport::CleanFolder(
				Settings ? Settings->OutlinerMeshSubFolder : TEXT("MazeMeshes"));
			bFolderPerRoom = Settings ? Settings->bOutlinerFolderPerRoom : true;
			bSplitByBand = Settings ? Settings->bSplitByDepthBand : true;

			// Never zero here, whatever the setting says. Zero means "do not flush" for the bake,
			// where keeping things loaded can be a choice; the moved assets are never needed
			// again, and keeping them loaded is exactly what took the device down.
			PerFlush = (Settings && Settings->RoomsPerFlush > 0) ? Settings->RoomsPerFlush : 16;
		}

		/** Prefix every room level of this maze starts with. "R_" keeps anything else out. */
		FString LevelPrefix() const
		{
			return MazeName.IsEmpty() ? FString(TEXT("L_R_")) : FString::Printf(TEXT("L_%s_R_"), *MazeName);
		}

		FString MeshPrefix() const
		{
			return MazeName.IsEmpty() ? FString(TEXT("SM_R_")) : FString::Printf(TEXT("SM_%s_R_"), *MazeName);
		}
	};

	/** Detaches one room's level if it is on the map, and clears its Outliner folders. */
	void TakeOffMap(UWorld* World, const FMazeFolders& Folders, const FString& LevelAsset,
	                int32& InOutDetached, int32& InOutFoldersRemoved, bool& bOutHoldsDecor)
	{
		bOutHoldsDecor = false;
		if (!World)
		{
			return;
		}

		if (ULevelStreaming* Streaming = FindStreamingLevelByPath(World, Folders.LevelRoot / LevelAsset))
		{
			if (ULevel* Loaded = Streaming->GetLoadedLevel())
			{
				bOutHoldsDecor = HoldsHandPlacedActors(Loaded);
				InOutDetached += UEditorLevelUtils::RemoveLevelFromWorld(Loaded) ? 1 : 0;
			}
		}

		if (Folders.bFolderPerRoom && !Folders.FolderRoot.IsEmpty())
		{
			FActorFolders& ActorFolders = FActorFolders::Get();
			const FString RoomFolder = Folders.FolderRoot / LevelAsset;

			// Deepest first: deleting a parent before its children leaves the folder container
			// to decide what happens to them.
			if (!Folders.MeshSubFolder.IsEmpty())
			{
				ActorFolders.DeleteFolder(*World, FFolder(FFolder::GetInvalidRootObject(),
					FName(*(RoomFolder / Folders.MeshSubFolder))));
				++InOutFoldersRemoved;
			}

			ActorFolders.DeleteFolder(*World, FFolder(FFolder::GetInvalidRootObject(),
				FName(*RoomFolder)));
			++InOutFoldersRemoved;
		}
	}

	/** The manifest by its link, or where the build puts it when the link is lost. */
	const UMazeWorldManifest* ResolveManifest(const UMazeGridAsset& Asset)
	{
		if (const UMazeWorldManifest* Linked = Asset.Manifest.LoadSynchronous())
		{
			return Linked;
		}

		const UMazeBuildSettings* Settings = Asset.BuildSettings.LoadSynchronous();
		const FString MazeName = Asset.GetSafeMazeName();
		const FString PackageName = MazeExport::ManifestPackageName(Settings, MazeName);

		if (!FPackageName::DoesPackageExist(PackageName))
		{
			return nullptr;
		}

		return LoadObject<UMazeWorldManifest>(nullptr, *FString::Printf(TEXT("%s.%s"),
			*PackageName, *MazeExport::ManifestAssetName(Settings, MazeName)));
	}

	/**
	 *  One room's worth of stale assets: its level, if there is one, and whatever meshes carry
	 *  the same room id. Kept together so a level and its meshes move in the same call, and the
	 *  level's references to them are fixed up once rather than through redirectors.
	 */
	struct FStaleUnit
	{
		FString RoomKey;
		TArray<FAssetData> Assets;
		bool bHasLevel = false;
	};

	/**
	 *  Every room level and mesh in this maze's folders that the manifest does not use.
	 *
	 *  Only the folders themselves, not their subfolders — that is what keeps Deprecated out —
	 *  and only names with this maze's room prefix, so nothing the designer keeps beside the
	 *  rooms is ever touched. Reads the asset registry only: nothing is loaded to be counted.
	 */
	TArray<FStaleUnit> FindStale(const FMazeFolders& Folders, const UMazeWorldManifest& Manifest)
	{
		TSet<FString> LiveLevels;
		TSet<FString> LiveRoomKeys;
		for (const FMazeRoomEntry& Entry : Manifest.Rooms)
		{
			LiveLevels.Add(MazeExport::LevelAssetName(Folders.MazeName, Entry.RoomId));
			LiveRoomKeys.Add(Entry.RoomId.ToString());
		}

		IAssetRegistry& Registry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TMap<FString, FStaleUnit> Units;

		// Levels.
		{
			TArray<FAssetData> Found;
			Registry.GetAssetsByPath(FName(*Folders.LevelRoot), Found, /*bRecursive*/ false);

			const FString Prefix = Folders.LevelPrefix();
			for (const FAssetData& Data : Found)
			{
				const FString Name = Data.AssetName.ToString();
				if (!Name.StartsWith(Prefix) || LiveLevels.Contains(Name))
				{
					continue;
				}

				// The room id is what follows "L_<maze>_": the prefix ends with "R_", which is
				// the start of the id itself.
				const FString RoomKey = Name.RightChop(Prefix.Len() - 2);

				FStaleUnit& Unit = Units.FindOrAdd(RoomKey);
				Unit.RoomKey = RoomKey;
				Unit.Assets.Add(Data);
				Unit.bHasLevel = true;
			}
		}

		// Meshes. A mesh is live if its room is live, whatever band or split setting made it —
		// a change of Split By Depth Band leaves old-format meshes behind, and those are stale.
		{
			TArray<FAssetData> Found;
			Registry.GetAssetsByPath(FName(*Folders.MeshRoot), Found, /*bRecursive*/ false);

			const FString Prefix = Folders.MeshPrefix();
			for (const FAssetData& Data : Found)
			{
				const FString Name = Data.AssetName.ToString();
				if (!Name.StartsWith(Prefix))
				{
					continue;
				}

				// "SM_<maze>_R_003_002_Play" -> "R_003_002". Room ids from the uniform grid are
				// "R_<x>_<z>", so the key is the first three underscore-separated parts.
				const FString Tail = Name.RightChop(Prefix.Len() - 2);
				TArray<FString> Parts;
				Tail.ParseIntoArray(Parts, TEXT("_"));
				if (Parts.Num() < 3)
				{
					continue;
				}

				const FString RoomKey = Parts[0] + TEXT("_") + Parts[1] + TEXT("_") + Parts[2];
				const bool bLive = LiveRoomKeys.Contains(RoomKey)
					&& (Folders.bSplitByBand ? Parts.Num() == 4 : Parts.Num() == 3);
				if (bLive)
				{
					continue;
				}

				FStaleUnit& Unit = Units.FindOrAdd(RoomKey);
				Unit.RoomKey = RoomKey;
				Unit.Assets.Add(Data);
			}
		}

		TArray<FStaleUnit> Result;
		Units.GenerateValueArray(Result);
		Result.Sort([](const FStaleUnit& A, const FStaleUnit& B) { return A.RoomKey < B.RoomKey; });
		return Result;
	}
}

// ============================================================================ reports

FString FMazeRetirementReport::ToString() const
{
	return FString::Printf(
		TEXT("rooms taken off the map %d (detached %d, outliner folders %d). Their levels and "
		     "meshes are still in the live folders — Move Stale Rooms To Deprecated tidies them"),
		Rooms, Detached, FoldersRemoved);
}

FString FMazeDeprecationReport::ToString() const
{
	return FString::Printf(
		TEXT("stale: levels %d, meshes %d; moved to %s: levels %d, meshes %d; unloaded again %d; "
		     "could not be moved %d; %d batch(es) in %.1f s%s"),
		StaleLevels, StaleMeshes, DeprecatedFolder, LevelsMoved, MeshesMoved, Unloaded, Failed,
		Batches, Seconds, bCancelled ? TEXT(" — CANCELLED, the rest is where it was") : TEXT(""));
}

// ============================================================================ during Apply

TArray<FName> FMazeRoomRetirement::SnapshotRoomIds(const UMazeWorldManifest* Manifest)
{
	TArray<FName> Ids;
	if (!Manifest)
	{
		return Ids;
	}

	Ids.Reserve(Manifest->Rooms.Num());
	for (const FMazeRoomEntry& Entry : Manifest->Rooms)
	{
		Ids.Add(Entry.RoomId);
	}

	return Ids;
}

FMazeRetirementReport FMazeRoomRetirement::Retire(const UMazeGridAsset* Asset,
                                                  const TArray<FName>& PreviousRoomIds)
{
	FMazeRetirementReport Report;
	if (!Asset || PreviousRoomIds.Num() == 0)
	{
		return Report;
	}

	TSet<FName> StillProduced;
	StillProduced.Reserve(Asset->Rooms.Num());
	for (const FMazeRoomDesc& Room : Asset->Rooms)
	{
		StillProduced.Add(Room.RoomId);
	}

	const FMazeFolders Folders(*Asset);
	UWorld* World = MazeExport::EditorWorld();

	for (const FName& RoomId : PreviousRoomIds)
	{
		if (StillProduced.Contains(RoomId))
		{
			continue;
		}

		++Report.Rooms;
		Report.RoomNames.Add(RoomId.ToString());

		bool bHoldsDecor = false;
		TakeOffMap(World, Folders, MazeExport::LevelAssetName(Folders.MazeName, RoomId),
			Report.Detached, Report.FoldersRemoved, bHoldsDecor);

		if (bHoldsDecor)
		{
			Report.RoomsHoldingDecor.Add(RoomId.ToString());
		}
	}

	if (Report.IsEmpty())
	{
		return Report;
	}

	UE_LOG(LogMazeForge, Log, TEXT("Retire: %s. Rooms: %s"),
		*Report.ToString(), *FString::Join(Report.RoomNames, TEXT(", ")));

	if (Report.RoomsHoldingDecor.Num() > 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Retire: %d level(s) taken off the map held actors this plugin did not place — "
			     "%s. That decor is no longer in the map. Its level is untouched in the live "
			     "folder; open it to take the decor back."),
			Report.RoomsHoldingDecor.Num(), *FString::Join(Report.RoomsHoldingDecor, TEXT(", ")));
	}

	return Report;
}

// ============================================================================ on request

bool FMazeRoomRetirement::CountStale(const UMazeGridAsset* Asset, int32& OutLevels, int32& OutMeshes)
{
	OutLevels = 0;
	OutMeshes = 0;

	const UMazeWorldManifest* Manifest = Asset ? ResolveManifest(*Asset) : nullptr;
	if (!Manifest)
	{
		return false;
	}

	const FMazeFolders Folders(*Asset);
	for (const FStaleUnit& Unit : FindStale(Folders, *Manifest))
	{
		OutLevels += Unit.bHasLevel ? 1 : 0;
		OutMeshes += Unit.Assets.Num() - (Unit.bHasLevel ? 1 : 0);
	}

	return true;
}

FMazeDeprecationReport FMazeRoomRetirement::MoveStaleToDeprecated(const UMazeGridAsset* Asset)
{
	FMazeDeprecationReport Report;

	const UMazeWorldManifest* Manifest = Asset ? ResolveManifest(*Asset) : nullptr;
	if (!Manifest)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Deprecate: %s has no manifest to compare its folders against. Build it first — "
			     "without one, every room level would look stale."), *GetNameSafe(Asset));
		return Report;
	}

	const FMazeFolders Folders(*Asset);
	const TArray<FStaleUnit> Units = FindStale(Folders, *Manifest);

	for (const FStaleUnit& Unit : Units)
	{
		Report.StaleLevels += Unit.bHasLevel ? 1 : 0;
		Report.StaleMeshes += Unit.Assets.Num() - (Unit.bHasLevel ? 1 : 0);
	}

	if (Units.Num() == 0)
	{
		UE_LOG(LogMazeForge, Log, TEXT("Deprecate: nothing stale in %s or %s."),
			*Folders.LevelRoot, *Folders.MeshRoot);
		return Report;
	}

	const double StartTime = FPlatformTime::Seconds();
	UWorld* World = MazeExport::EditorWorld();

	// Anything still on the map comes off it first: a world that is part of the editor world
	// cannot be renamed. Normally Apply has done this already; a crash may not have let it.
	int32 Detached = 0;
	int32 FoldersRemoved = 0;
	for (const FStaleUnit& Unit : Units)
	{
		if (!Unit.bHasLevel)
		{
			continue;
		}

		bool bHoldsDecor = false;
		TakeOffMap(World, Folders, MazeExport::LevelAssetName(Folders.MazeName, FName(*Unit.RoomKey)),
			Detached, FoldersRemoved, bHoldsDecor);
	}

	if (Detached > 0)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	// Batches of whole rooms, sized by Rooms Per Flush.
	TArray<TArray<const FStaleUnit*>> Batches;
	for (const FStaleUnit& Unit : Units)
	{
		if (Batches.Num() == 0 || Batches.Last().Num() >= Folders.PerFlush)
		{
			Batches.AddDefaulted();
		}
		Batches.Last().Add(&Unit);
	}

	FAssetToolsModule& AssetTools =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

	FScopedSlowTask Task(static_cast<float>(Batches.Num()),
		FText::Format(NSLOCTEXT("MazeForgeEditor", "DeprecateTask",
			"MazeForge: moving {0} stale room(s) to Deprecated"), FText::AsNumber(Units.Num())));
	Task.MakeDialog(/*bShowCancelButton*/ true);

	for (int32 BatchIndex = 0; BatchIndex < Batches.Num(); ++BatchIndex)
	{
		if (Task.ShouldCancel())
		{
			Report.bCancelled = true;
			break;
		}

		Task.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("MazeForgeEditor", "DeprecateBatch", "Batch {0} of {1}"),
			FText::AsNumber(BatchIndex + 1), FText::AsNumber(Batches.Num())));

		const double BatchStart = FPlatformTime::Seconds();

		TArray<FAssetRenameData> Renames;
		TArray<UObject*> Moved;

		for (const FStaleUnit* Unit : Batches[BatchIndex])
		{
			for (const FAssetData& Data : Unit->Assets)
			{
				UObject* Object = Data.GetAsset();
				if (!Object)
				{
					++Report.Failed;
					continue;
				}

				if (UWorld* AsWorld = Cast<UWorld>(Object))
				{
					if (HoldsHandPlacedActors(AsWorld->PersistentLevel))
					{
						Report.LevelsHoldingDecor.Add(Data.AssetName.ToString());
					}
				}

				const FString FromPath = FPackageName::GetLongPackagePath(Data.PackageName.ToString());
				Renames.Emplace(Object, FromPath / DeprecatedFolder, Data.AssetName.ToString());
				Moved.Add(Object);
			}
		}

		if (Renames.Num() == 0)
		{
			continue;
		}

		++Report.Batches;

		if (!AssetTools.Get().RenameAssets(Renames))
		{
			Report.Failed += Renames.Num();

			UE_LOG(LogMazeForge, Warning,
				TEXT("Deprecate: batch %d of %d — %d asset(s) could not be moved and were left "
				     "where they are."), BatchIndex + 1, Batches.Num(), Renames.Num());
			continue;
		}

		// RenameAssets saves what it moves on its own. Saved again only if it left something
		// dirty: saving twice was half of what made the first version slow, and a dirty
		// package cannot be unloaded, which was the other half of what made it crash.
		TArray<UPackage*> ToUnload;
		for (UObject* Object : Moved)
		{
			UPackage* Package = Object ? Object->GetPackage() : nullptr;
			if (!Package)
			{
				continue;
			}

			const bool bIsLevel = Object->IsA<UWorld>();
			(bIsLevel ? Report.LevelsMoved : Report.MeshesMoved) += 1;

			if (Package->IsDirty())
			{
				MazeExport::SavePackageToDisk(Package, Object,
					bIsLevel ? TEXT(".umap") : TEXT(".uasset"));
			}

			ToUnload.Add(Package);
		}

		int32 Flushed = 0;
		MazeExport::FlushBatch(ToUnload, nullptr, Flushed);
		Report.Unloaded += Flushed;

		// Per batch, on purpose. Whether RenameAssets costs per call or per asset is the one
		// thing the first run could not tell apart, and it decides whether larger batches are
		// faster or slower. These lines answer it.
		UE_LOG(LogMazeForge, Log, TEXT("Deprecate: batch %d of %d — %d asset(s) in %.1f s."),
			BatchIndex + 1, Batches.Num(), Renames.Num(), FPlatformTime::Seconds() - BatchStart);
	}

	Report.Seconds = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogMazeForge, Log, TEXT("Deprecate: %s"), *Report.ToString());

	if (Report.LevelsHoldingDecor.Num() > 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Deprecate: %d moved level(s) held actors this plugin did not place — %s. That "
			     "decor is in %s now, with its level. Open the level there to take it back."),
			Report.LevelsHoldingDecor.Num(), *FString::Join(Report.LevelsHoldingDecor, TEXT(", ")),
			DeprecatedFolder);
	}

	return Report;
}
