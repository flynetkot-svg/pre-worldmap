#include "Export/MazeLevelExporter.h"

#include "ActorEditorUtils.h"
#include "Actors/MazeRoomAnchor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Assets/MazeBuildSettings.h"
#include "Assets/MazeGridAsset.h"
#include "Assets/MazeObjectLibrary.h"
#include "Assets/MazeSpawnAsset.h"
#include "Assets/MazeWorldManifest.h"
#include "Data/MazeGrid.h"
#include "Data/MazeRoomDesc.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Export/MazeExportUtils.h"
#include "Export/MazeRoomRetirement.h"
#include "Export/MazeMeshBuilder_Faces.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "Hash/CityHash.h"
#include "MazeForgeCore.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "PhysicsEngine/BodySetup.h"
#include "Spawner/MazeObjectIdComponent.h"
#include "Spawner/MazePlacementRules.h"
#include "StaticMeshCompiler.h"
#include "UObject/GCObjectScopeGuard.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "MazeForgeEditor"

namespace
{
	/**
	 *  Part of every level hash. Raised whenever the export changes what it writes into a level,
	 *  so that levels written by an older build are rewritten once rather than trusted.
	 */
	constexpr int32 LevelFormatVersion = 1;

	/** A struct as the Details panel would copy it: every property, including ones added later. */
	template <typename TStruct>
	void AppendStructText(FString& Key, const TStruct& Value)
	{
		FString Text;
		TStruct::StaticStruct()->ExportText(Text, &Value, nullptr, nullptr, PPF_None, nullptr);
		Key += Text;
		Key += TEXT('|');
	}

	/**
	 *  Everything a room's level is written from, boiled down to one number.
	 *
	 *  The mesh hash (RoomHash) covers the geometry. On top of it a level carries the neighbour
	 *  list on its anchor, the objects standing in the room and the library types they are built
	 *  from, plus where and under which names things are written (Context). Two levels with the
	 *  same hash come out of the export identical, so one already on disk can be left alone.
	 *
	 *  An object is fitted against the geometry around it, and a large footprint can reach past
	 *  the one-cell skirt the mesh hash looks at. So in a room with objects the neighbours' mesh
	 *  hashes count too: an edit next door re-exports the level (cheap — the meshes are reused)
	 *  rather than risk leaving an object floating where a wall used to be.
	 */
	int64 ComputeLevelHash(const UMazeGridAsset& Asset, const FMazeRoomDesc& Room, int64 RoomHash,
		const TArray<int32>* RoomPlacements, const UMazeSpawnAsset* Spawns,
		const UMazeObjectLibrary* Library, const FString& Context)
	{
		FString Key = FString::Printf(TEXT("v%d|%s|%016llx|"),
			LevelFormatVersion, *Context, static_cast<uint64>(RoomHash));

		for (const FName& Neighbor : Room.Neighbors)
		{
			Key += Neighbor.ToString();
			Key += TEXT(',');
		}
		Key += TEXT('|');

		if (RoomPlacements && Spawns)
		{
			for (const int32 Index : *RoomPlacements)
			{
				const FMazePlacement& Placement = Spawns->Placements[Index];
				AppendStructText(Key, Placement);

				if (const FMazeObjectType* Type = Library ? Library->FindType(Placement.TypeId) : nullptr)
				{
					AppendStructText(Key, *Type);
				}
				else
				{
					Key += TEXT("no type|");
				}
			}

			for (const FName& Neighbor : Room.Neighbors)
			{
				if (const FMazeRoomDesc* Other = Asset.FindRoom(Neighbor))
				{
					Key += FString::Printf(TEXT("%016llx,"),
						static_cast<uint64>(Asset.ComputeRoomHash(*Other)));
				}
			}
		}

		return static_cast<int64>(CityHash64(reinterpret_cast<const char*>(*Key),
			static_cast<uint32>(Key.Len() * sizeof(TCHAR))));
	}
}

FString FMazeExportReport::ToString() const
{
	// Retirement is mentioned only when it happened. A permanent "retired 0" would train the
	// eye to skip the one line that says somebody's levels have just moved.
	const FString Retired = (RetiredRooms > 0)
		? FString::Printf(TEXT("rooms taken off the map %d (assets left in place — Move Stale "
		                       "Rooms To Deprecated tidies them); "), RetiredRooms)
		: FString();

	return Retired + FString::Printf(
		TEXT("rooms %d, levels written %d, unchanged and left alone %d, meshes %d (of them reused %d), ")
		TEXT("triangles %d, collision boxes %d; ")
		TEXT("objects %d (snapped %d, re-anchored %d, skipped %d, unknown type %d, ")
		TEXT("facing mode without a wall %d); ")
		TEXT("generated actors replaced %d (hand resizing lost %d), user actors preserved %d; ")
		TEXT("save failures %d; packages unloaded %d; in %.2f s"),
		Rooms, Levels, ReusedLevels, Meshes, ReusedMeshes, Triangles, CollisionBoxes,
		Objects, SnappedObjects, ReanchoredObjects, SkippedObjects, OrphanObjects,
		FacingModeIgnored,
		ReplacedActors, HandResizedActors, PreservedActors,
		FailedPackages, FlushedPackages, Seconds);
}

bool FMazeLevelExporter::ExportRooms(UMazeGridAsset* Asset, FMazeExportReport& OutReport, bool bForceAll)
{
	if (!Asset || Asset->Rooms.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Export: there are no rooms. Press Slice Into Rooms first."));
		return false;
	}

	const UMazeBuildSettings* Settings = Asset->BuildSettings.LoadSynchronous();

	// Every path and every name this build writes carries the maze's compartment. Empty name,
	// no compartment, and the result is what it always was.
	const FString MazeName = Asset->GetSafeMazeName();

	const FString LevelRoot = MazeExport::ScopedRoot(
		Settings ? Settings->LevelPackageRoot : TEXT("/Game/MazeForge/Maps"), MazeName);
	const FString MeshRoot = MazeExport::ScopedRoot(
		Settings ? Settings->MeshPackageRoot : TEXT("/Game/MazeForge/Meshes"), MazeName);
	const FName GeneratedTag = Settings ? Settings->GeneratedTag : FName(TEXT("MazeForge.Generated"));
	const bool bSplitByBand = Settings ? Settings->bSplitByDepthBand : true;
	const int32 RoomsPerFlush = Settings ? Settings->RoomsPerFlush : 16;

	// Packages of finished rooms waiting to be unloaded. They pile up until the end of a batch.
	TArray<UPackage*> PendingUnload;

	// The Outliner gets the same compartment: with two mazes in a project, "Levels" holding both
	// their room folders side by side is exactly the pile the maze name exists to avoid.
	//
	// Built by the shared helper because the detach has to delete exactly these folders, and a
	// second copy of the path arithmetic would leave folders behind the day one of them changed.
	const FString FolderRoot = MazeExport::OutlinerRoot(
		Settings ? Settings->OutlinerRootFolder : TEXT("Levels"), MazeName);
	const FString MeshSubFolder = MazeExport::CleanFolder(
		Settings ? Settings->OutlinerMeshSubFolder : TEXT("MazeMeshes"));
	const FString ObjectSubFolder = MazeExport::CleanFolder(
		Settings ? Settings->OutlinerObjectSubFolder : TEXT("MazeObjects"));
	const bool bFolderPerRoom = Settings ? Settings->bOutlinerFolderPerRoom : true;

	const FMazeGrid& Grid = Asset->Grid;

	// ------------------------------------------------------- objects to place

	UMazeSpawnAsset* Spawns = Asset->Spawns.LoadSynchronous();
	UMazeObjectLibrary* Library = Spawns ? Spawns->Library.LoadSynchronous() : nullptr;

	// Both are reached through soft pointers, which are not references as far as the collector is
	// concerned. The export unloads batches and collects garbage as it goes, so without these two
	// guards the spawn asset could be swept up halfway through — taking with it every id handed
	// out so far, for objects already written into levels on disk.
	FGCObjectScopeGuard SpawnsGuard(Spawns);
	FGCObjectScopeGuard LibraryGuard(Library);

	// Bucketed once instead of scanning every placement inside every room: two hundred rooms
	// times a few hundred objects is the kind of quadratic that only shows itself on the maze
	// nobody wants to wait for.
	//
	// The origin cell decides which room owns an object, not the footprint. Something that hangs
	// over a boundary is built once, in the room it starts in, and streams with that room — the
	// deliberate answer to "one pipe across ten levels", which this project does not have to
	// solve because it does not have to allow it.
	TMap<FName, TArray<int32>> PlacementsByRoom;

	// Filled in the spawn loop below, not here: a transition point is published only if it was
	// actually built, and whether it fits is not known until the geometry is checked.
	TArray<FMazeTransitionPoint> StagedTransitions;

	// Stands in for a room that has no objects, so the loop below can take a reference in
	// both cases. A ternary between a reference and a temporary would copy the array once
	// per room, which is the sort of thing that never shows up until the maze is large.
	const TArray<int32> NoPlacements;

	if (Spawns)
	{
		for (int32 Index = 0; Index < Spawns->Placements.Num(); ++Index)
		{
			const FIntPoint& Cell = Spawns->Placements[Index].CellXZ;

			const FMazeRoomDesc* Owner = MazeRooms::FindAtXZ(Asset->Rooms, Cell);

			if (Owner)
			{
				PlacementsByRoom.FindOrAdd(Owner->RoomId).Add(Index);
			}
			else
			{
				++OutReport.SkippedObjects;
				UE_LOG(LogMazeForge, Warning,
					TEXT("Export: the object at X %d Z %d lies outside every room. Not spawned."),
					Cell.X, Cell.Y);
			}
		}
	}

	/** Set when at least one placement was given a number, which is what makes the asset worth saving. */
	bool bIdsAssigned = false;

	const double StartTime = FPlatformTime::Seconds();

	FScopedSlowTask SlowTask(static_cast<float>(Asset->Rooms.Num()),
		LOCTEXT("ExportingRooms", "MazeForge: exporting rooms to levels"));
	SlowTask.MakeDialog(true);

	FMazeMeshBuilder_Faces Builder;

	// Reuse is decided per room, not for the maze as a whole. One edited room used to make every
	// baked mesh suspect, and 207 untouched rooms were rebuilt to be sure. Each room now carries
	// a hash of what it is made of, and only the ones whose hash moved are rebuilt.
	const int32 FreshRooms = Asset->CountBakedRooms();

	UE_LOG(LogMazeForge, Log,
		TEXT("Export: %d of %d rooms are baked and unchanged — those are taken from disk, "
		     "the remaining %d are built here."),
		FreshRooms, Asset->Rooms.Num(), Asset->Rooms.Num() - FreshRooms);

	// The manifest is rebuilt in full: it is derived data, there are no hand edits in it.
	// Where it lives is decided in one place, shared with Attach — see ManifestPackageName.
	const FString ManifestName = MazeExport::ManifestAssetName(Settings, MazeName);
	const FString ManifestPackageName = MazeExport::ManifestPackageName(Settings, MazeName);

	UPackage* ManifestPackage = CreatePackage(*ManifestPackageName);
	ManifestPackage->FullyLoad();

	UMazeWorldManifest* Manifest = FindObject<UMazeWorldManifest>(ManifestPackage, *ManifestName);
	if (!Manifest)
	{
		Manifest = NewObject<UMazeWorldManifest>(ManifestPackage, FName(*ManifestName),
			RF_Public | RF_Standalone);
	}
	// Staged, not written straight into the manifest. The manifest is the only thing the game
	// ever sees, and a cancelled export used to save a truncated one: stop at room 40 of 208 and
	// the shipped world contained 40 rooms, with no error anywhere to say so.
	TArray<FMazeRoomEntry> StagedRooms;
	StagedRooms.Reserve(Asset->Rooms.Num());

	bool bCancelled = false;
	// Taken before anything is written, because the manifest is about to be replaced whole and
	// this is the only record of which rooms the last build actually produced. Without it,
	// "which rooms has this re-slicing abolished" has no answer at all — which is why their
	// levels used to stay in the map for ever.
	const TArray<FName> PreviousRoomIds = FMazeRoomRetirement::SnapshotRoomIds(Manifest);

	// A level left alone still needs its manifest entry and its transition points, and the
	// previous manifest is where they are. Read before it is replaced.
	TMap<FName, FMazeRoomEntry> PreviousEntries;
	for (const FMazeRoomEntry& Entry : Manifest->Rooms)
	{
		PreviousEntries.Add(Entry.RoomId, Entry);
	}

	TMap<FName, TArray<FMazeTransitionPoint>> PreviousTransitions;
	for (const FMazeTransitionPoint& Point : Manifest->Transitions)
	{
		if (const FMazeRoomDesc* Owner = MazeRooms::FindAtXZ(Asset->Rooms, Point.CellXZ))
		{
			PreviousTransitions.FindOrAdd(Owner->RoomId).Add(Point);
		}
	}

	// Where and under which names a level's contents are written. Change any of it and every
	// level is out of date, even though not one cell moved.
	const FString CommonContext = FString::Printf(TEXT("%s|%s|%s|%s|%s|%d"),
		*MeshRoot, *GeneratedTag.ToString(), *FolderRoot, *MeshSubFolder, *ObjectSubFolder,
		bFolderPerRoom ? 1 : 0);

	// Written to the asset only once the export has finished, like the bake's hashes: a cancelled
	// export leaves the manifest as it was, and a level marked current against a manifest that
	// never heard of it would lose its transition points on the next pass.
	TMap<FName, int64> NewLevelHashes;

	Manifest->WorldBounds = Grid.GetWorldBounds();
	Manifest->PlayPlaneY = static_cast<float>(Grid.GetPlayPlaneY());

	const EMazeDepthBand AllBands[3] = {
		EMazeDepthBand::Background, EMazeDepthBand::Play, EMazeDepthBand::Foreground
	};

	for (const FMazeRoomDesc& Room : Asset->Rooms)
	{
		if (SlowTask.ShouldCancel())
		{
			UE_LOG(LogMazeForge, Warning, TEXT("Export cancelled by the user."));
			bCancelled = true;
			break;
		}
		SlowTask.EnterProgressFrame(1.0f);

		++OutReport.Rooms;

		const FVector RoomOrigin = Grid.GetBoxWorldBounds(Room.MinXZ, Room.MaxXZ).Min;
		const TArray<int32>* RoomPlacements = PlacementsByRoom.Find(Room.RoomId);

		const FString LevelName = MazeExport::LevelAssetName(MazeName, Room.RoomId);
		const FString LevelPackageName = LevelRoot / LevelName;
		const FString LevelContext = CommonContext + TEXT("|") + LevelPackageName;

		// Computed once and reused below: the hash walks the room's cells plus a one-cell skirt,
		// and doing that twice per room would be the most expensive part of the export.
		const int64 RoomHash = Asset->ComputeRoomHash(Room);

		// ------------------------------------------------------- unchanged level

		// Nothing the level is made of has moved since it was written, the file is still there
		// and the last manifest knows the room: the level is left exactly as it is. Not loaded,
		// not rewritten — a repeated Apply used to rewrite all 124 levels of Saboteur, 8 seconds
		// and 124 changed .umap files in git for nothing, and it re-spawned every generated actor.
		const int64* StoredLevelHash = Asset->ExportedLevelHashes.Find(Room.RoomId);
		const FMazeRoomEntry* PreviousEntry = PreviousEntries.Find(Room.RoomId);

		if (!bForceAll && StoredLevelHash && PreviousEntry
			&& *StoredLevelHash == ComputeLevelHash(*Asset, Room, RoomHash, RoomPlacements,
				Spawns, Library, LevelContext)
			&& FPackageName::DoesPackageExist(LevelPackageName))
		{
			StagedRooms.Add(*PreviousEntry);
			if (const TArray<FMazeTransitionPoint>* Points = PreviousTransitions.Find(Room.RoomId))
			{
				StagedTransitions.Append(*Points);
			}

			NewLevelHashes.Add(Room.RoomId, *StoredLevelHash);
			++OutReport.ReusedLevels;
			continue;
		}

		// ------------------------------------------------------- room meshes

		TArray<UStaticMesh*> RoomMeshes;
		int32 RoomTriangles = 0;

		const int64* BakedHash = Asset->BakedRoomHashes.Find(Room.RoomId);
		const bool bRoomBaked = BakedHash && *BakedHash == RoomHash;

		// Cleared by a failed write. Only a room that reached the disk in full may be recorded as
		// baked — otherwise the next export would take a half-written room for a finished one.
		bool bRoomComplete = true;

		const int32 BandCount = bSplitByBand ? 3 : 1;
		for (int32 Index = 0; Index < BandCount; ++Index)
		{
			const EMazeDepthBand Band = bSplitByBand ? AllBands[Index] : EMazeDepthBand::Play;
			const FString AssetName = MazeExport::MeshAssetName(
				MazeName, Room.RoomId, Band, bSplitByBand);

			UStaticMesh* Mesh = nullptr;
			int32 Triangles = 0;
			int32 Boxes = 0;

			if (bRoomBaked)
			{
				// Quietly: a missing mesh here is a normal situation, handled by the branch
				// below, not by half a log of loader warnings.
				Mesh = LoadObject<UStaticMesh>(nullptr,
					*MazeExport::MeshObjectPath(MeshRoot, AssetName),
					nullptr, LOAD_NoWarn | LOAD_Quiet);
			}

			if (Mesh)
			{
				// The mesh may have arrived from disk still in asynchronous build: until that
				// finishes RenderData must not be read, and we need the triangles for the anchor.
				FStaticMeshCompilingManager::Get().FinishCompilation({ Mesh });

				Triangles = Mesh->GetNumTriangles(0);
				Boxes = Mesh->GetBodySetup() ? Mesh->GetBodySetup()->AggGeom.BoxElems.Num() : 0;

				++OutReport.ReusedMeshes;
			}
			else
			{
				// Nothing baked: either the bake was never run, or it did not reach this room,
				// or the grid was edited. We build it ourselves — the export has to work from
				// a single button too.
				FMazeBakeRequest Request;
				Request.Grid = &Grid;
				Request.Room = &Room;
				Request.Settings = Settings;
				Request.Band = Band;
				Request.AssetName = AssetName;
				Request.PackageName = MeshRoot / AssetName;

				FMazeBakeResult Result;
				if (!Builder.Build(Request, Result) || !Result.Mesh)
				{
					if (Result.bFailed)
					{
						++OutReport.FailedPackages;
						bRoomComplete = false;
					}

					continue;
				}

				// The level will reference the mesh, so the mesh is saved first.
				if (!MazeExport::SavePackageToDisk(Result.Mesh->GetPackage(), Result.Mesh,
					FPackageName::GetAssetPackageExtension()))
				{
					++OutReport.FailedPackages;
					bRoomComplete = false;
					continue;
				}

				Mesh = Result.Mesh;
				Triangles = Result.Triangles();
				Boxes = Result.CollisionBoxes;
			}

			RoomMeshes.Add(Mesh);
			PendingUnload.AddUnique(Mesh->GetPackage());
			RoomTriangles += Triangles;

			++OutReport.Meshes;
			OutReport.Triangles += Triangles;
			OutReport.CollisionBoxes += Boxes;
		}

		// The export builds meshes when the bake was skipped, so it records them the same way the
		// bake does. Without this, running Export twice in a row would rebuild the same geometry
		// the second time — the exact double work the hashes exist to remove.
		if (!bRoomBaked && bRoomComplete && RoomMeshes.Num() > 0)
		{
#if WITH_EDITOR
			Asset->Modify();
#endif
			Asset->BakedRoomHashes.Add(Room.RoomId, RoomHash);
			Asset->InvalidateBakedRoomCache();
		}

		if (RoomMeshes.Num() == 0)
		{
			// No geometry means no level is written, and no level means there is nowhere to put
			// the objects. Saying so is the point: this is the one path where an object could
			// disappear without anybody having touched it.
			if (RoomPlacements)
			{
				OutReport.SkippedObjects += RoomPlacements->Num();

				UE_LOG(LogMazeForge, Warning,
					TEXT("Export: room %s holds %d objects but no geometry, so no level was "
					     "written for it. They were not spawned."),
					*Room.RoomId.ToString(), RoomPlacements->Num());
			}

			continue;
		}

		// ------------------------------------------------------- room level

		UPackage* LevelPackage = CreatePackage(*LevelPackageName);
		LevelPackage->FullyLoad();

		UWorld* RoomWorld = UWorld::FindWorldInPackage(LevelPackage);
		if (!RoomWorld)
		{
			RoomWorld = UWorld::CreateWorld(EWorldType::Inactive, false,
				FName(*LevelName), LevelPackage, /*bAddToRoot*/ false);
		}

		if (!RoomWorld)
		{
			++OutReport.FailedPackages;
			continue;
		}

		// UWorld::CreateWorld gives the new world only RF_Transactional, while marking the
		// package as a map right away. We save with TopLevelFlags = RF_Public | RF_Standalone,
		// so without those flags the world does not make it into the export list: we end up with
		// a "map package with no map inside", and SavePackage trips an ensure before it even
		// writes the file.
		RoomWorld->SetFlags(RF_Public | RF_Standalone);

		// Nothing here touches RoomWorld->WorldType, and that is deliberate.
		//
		// "UWorld::DestroyActor: World has no context!" on every re-export looks like a world
		// whose type is wrong, and it is not. UWorld::PostLoad already gives a world read off
		// disk EWorldType::Inactive; a room level that is currently ATTACHED to the editor world
		// is loaded through level streaming instead and gets the owning world's type, Editor. It
		// is a live world, and the warning is the engine noticing that a sublevel has no world
		// context of its own — which no sublevel ever has.
		//
		// Setting it to Inactive silences the line and tells the engine that a world the editor
		// is holding open is a dormant asset: component registration stops broadcasting and
		// BeginDestroy starts calling CleanupWorld on it. That is how one line of tidying up the
		// log destroyed the maze and built nothing in its place.
		//
		// The noise is the engine's, on a false positive, and it stays.

		// The rule for a safe re-export: we only remove our own actors, the ones marked with the
		// tag. Everything the designer put into the room survives a rebuild of the maze.
		TArray<AActor*> ToRemove;
		for (AActor* Existing : RoomWorld->PersistentLevel->Actors)
		{
			if (!Existing)
			{
				continue;
			}

			if (Existing->Tags.Contains(GeneratedTag))
			{
				ToRemove.Add(Existing);
			}
			else if (Existing->IsEditable()
				&& Existing->IsListedInSceneOutliner()
				&& !Existing->IsA<AWorldSettings>()
				&& !FActorEditorUtils::IsABuilderBrush(Existing))
			{
				// We only count what the designer could actually have placed. The housekeeping
				// actors of a new level — WorldSettings, the builder brush and everything else
				// that is not in the Outliner — would otherwise make the report claim
				// "preserved decoration" on empty rooms.
				++OutReport.PreservedActors;
			}
		}

		for (AActor* Doomed : ToRemove)
		{
			// The one thing worth saying goodbye to out loud. The size of a spawned object comes
			// from its type and from nowhere else, so a size that differs from the type's was
			// typed into the level by hand — and this destroy is about to throw it away without
			// being asked.
			if (const UMazeObjectIdComponent* Id =
					Doomed->FindComponentByClass<UMazeObjectIdComponent>())
			{
				const FMazeObjectType* WasType = Library ? Library->FindType(Id->TypeId) : nullptr;
				const FVector FromType = WasType ? WasType->Scale : FVector::OneVector;
				const FVector Scale = Doomed->GetActorScale3D();

				if (!Scale.Equals(FromType, 0.01f))
				{
					++OutReport.HandResizedActors;

					UE_LOG(LogMazeForge, Warning,
						TEXT("Export: '%s' in room %s had been resized by hand to "
						     "%.2f x %.2f x %.2f. The rebuild puts it back to the %.2f x %.2f "
						     "x %.2f its type asks for — set the size on the type to keep it."),
						*Doomed->GetActorNameOrLabel(), *Room.RoomId.ToString(),
						Scale.X, Scale.Y, Scale.Z, FromType.X, FromType.Y, FromType.Z);
				}
			}

			RoomWorld->EditorDestroyActor(Doomed, false);
			++OutReport.ReplacedActors;
		}

		// The room's folder in the Outliner and a subfolder for the generated geometry.
		// The designer's decoration stays outside them, so "hide the meshes but not the level"
		// is one eye icon on the subfolder.
		FName RoomFolder;
		FName MeshFolder;
		FName ObjectFolder;
		if (!FolderRoot.IsEmpty())
		{
			const FString RoomPath = bFolderPerRoom ? (FolderRoot / LevelName) : FolderRoot;

			RoomFolder = FName(*RoomPath);
			MeshFolder = MeshSubFolder.IsEmpty()
				? RoomFolder
				: FName(*(RoomPath / MeshSubFolder));
			ObjectFolder = ObjectSubFolder.IsEmpty()
				? RoomFolder
				: FName(*(RoomPath / ObjectSubFolder));
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.OverrideLevel = RoomWorld->PersistentLevel;
		SpawnParams.ObjectFlags = RF_Transactional;

		for (UStaticMesh* Mesh : RoomMeshes)
		{
			AStaticMeshActor* MeshActor = RoomWorld->SpawnActor<AStaticMeshActor>(
				AStaticMeshActor::StaticClass(), FTransform(RoomOrigin), SpawnParams);

			if (!MeshActor)
			{
				continue;
			}

			UStaticMeshComponent* Component = MeshActor->GetStaticMeshComponent();
			Component->SetMobility(EComponentMobility::Static);
			Component->SetStaticMesh(Mesh);

			MeshActor->Tags.Add(GeneratedTag);
#if WITH_EDITOR
			MeshActor->SetActorLabel(Mesh->GetName());
			if (!MeshFolder.IsNone())
			{
				MeshActor->SetFolderPath(MeshFolder);
			}
#endif
		}

		AMazeRoomAnchor* Anchor = RoomWorld->SpawnActor<AMazeRoomAnchor>(
			AMazeRoomAnchor::StaticClass(), FTransform(RoomOrigin), SpawnParams);

		if (Anchor)
		{
			Anchor->RoomId = Room.RoomId;
			Anchor->WorldBounds = Room.WorldBounds;
			Anchor->Neighbors = Room.Neighbors;
			Anchor->Depth = Grid.Depth;
			Anchor->MinXZ = Room.MinXZ;
			Anchor->MaxXZ = Room.MaxXZ;
			Anchor->EstimatedTriangles = RoomTriangles;
			Anchor->Tags.Add(GeneratedTag);
#if WITH_EDITOR
			Anchor->SetActorLabel(FString::Printf(TEXT("Anchor_%s"), *Room.RoomId.ToString()));
			if (!RoomFolder.IsNone())
			{
				// The anchor goes at the root of the room folder, not among the meshes:
				// that way you see it straight away.
				Anchor->SetFolderPath(RoomFolder);
			}
#endif
		}

		// ------------------------------------------------------- objects

		for (const int32 Index : (RoomPlacements ? *RoomPlacements : NoPlacements))
		{
			const FMazePlacement& Placement = Spawns->Placements[Index];

			const FMazeObjectType* Type = Library ? Library->FindType(Placement.TypeId) : nullptr;
			if (!Type)
			{
				++OutReport.OrphanObjects;
				UE_LOG(LogMazeForge, Warning,
					TEXT("Export: the object at X %d Z %d is of type '%s', which is not in the "
					     "library. Not spawned."),
					Placement.CellXZ.X, Placement.CellXZ.Y, *Placement.TypeId.ToString());
				continue;
			}

			const bool bIsTransition = Type->TransitionRole != EMazeTransitionRole::None;

			UClass* ObjectClass = Type->ActorClass.LoadSynchronous();
			if (!ObjectClass && !bIsTransition)
			{
				++OutReport.OrphanObjects;
				UE_LOG(LogMazeForge, Warning,
					TEXT("Export: object type '%s' has no actor class set, so there is nothing "
					     "to spawn at X %d Z %d."),
					*Type->TypeId.ToString(), Placement.CellXZ.X, Placement.CellXZ.Y);
				continue;
			}

			const int32 SliceY = MazePlacement::BandSliceY(Grid, Placement.Band);

			// The brush lets the hand be wrong; the export does not let the world be wrong. The
			// placement is measured against the geometry as it stands NOW, because the wall it
			// was leaning on may well have been redrawn since it was put down.
			// Not called Anchor: the room's own anchor actor is already a local by that name a
			// little further up, and shadowing it compiles into a warning-as-error here.
			EMazeAnchorKind PlacedAnchor = Placement.Anchor;
			bool bReanchored = false;

			if (!MazePlacement::Fits(Grid, *Type, Placement.CellXZ, SliceY, PlacedAnchor))
			{
				if (!MazePlacement::FindAnchor(Grid, *Type, Placement.CellXZ, SliceY, PlacedAnchor))
				{
					++OutReport.SkippedObjects;
					UE_LOG(LogMazeForge, Warning,
						TEXT("Export: '%s' at X %d Z %d does not fit — %s. Not spawned."),
						*Type->TypeId.ToString(), Placement.CellXZ.X, Placement.CellXZ.Y,
						*MazePlacement::DescribeMisfit(Grid, *Type, Placement.CellXZ, SliceY));
					continue;
				}

				bReanchored = true;
				++OutReport.ReanchoredObjects;

				UE_LOG(LogMazeForge, Warning,
					TEXT("Export: '%s' at X %d Z %d lost the anchor it was placed on and took "
					     "another. Worth a look at which way it now faces."),
					*Type->TypeId.ToString(), Placement.CellXZ.X, Placement.CellXZ.Y);
			}

			// "Away from wall" with no wall to be away from. The object is spawned and keeps the
			// angle set on the type, which is exactly what Fixed would have given it — so the
			// mode a designer deliberately chose did nothing, and silence here is what leaves him
			// turning the same torch round and round wondering why it will not listen.
			if (Type->FacingMode == EMazeFacingMode::AwayFromWall
				&& PlacedAnchor != EMazeAnchorKind::Wall)
			{
				++OutReport.FacingModeIgnored;

				UE_LOG(LogMazeForge, Warning,
					TEXT("Export: '%s' at X %d Z %d asks to face away from a wall, but its anchor "
					     "is %s. It was given the fixed angle from the type."),
					*Type->TypeId.ToString(), Placement.CellXZ.X, Placement.CellXZ.Y,
					*StaticEnum<EMazeAnchorKind>()->GetNameStringByValue(
						static_cast<int64>(PlacedAnchor)));
			}

			// A re-anchored object keeps neither its rotation nor the reason for it: the stored
			// angle was resolved against the anchor it has just lost.
			const FRotator Rotation = bReanchored
				? MazePlacement::ResolveRotation(Grid, *Type, Placement.CellXZ, SliceY, PlacedAnchor,
					Grid.NumCells())
				: Placement.Rotation;

			// The anchor actually in use, which is not always the one stored: a re-anchored
			// object has just lost the one it was placed on. Both WorldLocation and ContactFace
			// read the anchor off the placement, so they are handed the current one — before
			// this, a crate that lost its floor was still positioned as though it had one.
			FMazePlacement Resolved = Placement;
			Resolved.Anchor = PlacedAnchor;

			const FVector Location = MazePlacement::WorldLocation(Grid, *Type, Resolved);

			// Scale goes on at spawn, before anything measures the actor: the snap below reads
			// its bounds, and the bounds of a crate at twice the size are twice the size.
			AActor* Object = ObjectClass
				? RoomWorld->SpawnActor<AActor>(ObjectClass,
					FTransform(Rotation, Location, Type->Scale), SpawnParams)
				: nullptr;

			if (!Object && ObjectClass)
			{
				++OutReport.SkippedObjects;
				UE_LOG(LogMazeForge, Warning,
					TEXT("Export: could not spawn '%s' at X %d Z %d."),
					*Type->TypeId.ToString(), Placement.CellXZ.X, Placement.CellXZ.Y);
				continue;
			}

			if (Object && Type->bSnapToAnchorSurface)
			{
				// Measured, not assumed. Where a mesh sits relative to its own origin is a fact
				// about the asset, and the asset lives in the game module; the rules can only
				// say which surface it should touch. Non-colliding components included, or a
				// prop built from plain meshes measures as nothing and never moves.
				const FBox Bounds = Object->GetComponentsBoundingBox(true);

				const FVector Delta = MazePlacement::ContactSnapDelta(
					Bounds, MazePlacement::ContactFace(Grid, *Type, Resolved), Location);

				if (!Delta.IsNearlyZero())
				{
					Object->SetActorLocation(Location + Delta);
					++OutReport.SnappedObjects;
				}
			}

			// The number is handed out here and nowhere earlier, so that having an id keeps
			// meaning "the export built this" and not merely "somebody once drew it". An Entry
			// point with no actor class earns one too: what the export built for it is the
			// coordinate in the manifest, which is every bit as real as an actor.
			const bool bWasUnnumbered = Placement.Id == 0;
			const int32 PlacementId = Spawns->AssignId(Index);
			bIdsAssigned |= bWasUnnumbered;

			if (Object)
			{
				Object->Tags.Add(GeneratedTag);

				UMazeObjectIdComponent* IdComponent = NewObject<UMazeObjectIdComponent>(Object);
				IdComponent->PlacementId = PlacementId;
				IdComponent->TypeId = Type->TypeId;
				IdComponent->RoomId = Room.RoomId;
				Object->AddInstanceComponent(IdComponent);
				IdComponent->RegisterComponent();

				++OutReport.Objects;

#if WITH_EDITOR
				Object->SetActorLabel(FString::Printf(TEXT("%s_%d"),
					*Type->TypeId.ToString(), PlacementId));

				if (!ObjectFolder.IsNone())
				{
					Object->SetFolderPath(ObjectFolder);
				}
#endif
			}

			if (bIsTransition)
			{
				// A transition outside the play band is built, spawns, looks right in the viewport
				// and can never work: the player only ever exists in the play band, so a Gate
				// there is a trigger nothing can enter and an Entry there teleports him off the
				// plane he moves in. From the side the two bands overlap on screen exactly, which
				// is what makes this cost an afternoon instead of a second.
				//
				// Not a refusal — the placement is legal and the band is a deliberate choice
				// everywhere else. Just impossible to miss.
				if (Placement.Band != EMazeDepthBand::Play)
				{
					UE_LOG(LogMazeForge, Warning,
						TEXT("Export: %s %d at X %d Z %d is in the %s band, not Play. The player "
						     "never goes there, so %s. Erase it and place it again with "
						     "Paint Band = Play."),
						Type->TransitionRole == EMazeTransitionRole::Gate ? TEXT("gate") : TEXT("entry"),
						PlacementId, Placement.CellXZ.X, Placement.CellXZ.Y,
						Placement.Band == EMazeDepthBand::Background
							? TEXT("Background") : TEXT("Foreground"),
						Type->TransitionRole == EMazeTransitionRole::Gate
							? TEXT("he can never walk into it")
							: TEXT("arriving there puts him off the play plane"));
				}

				FMazeTransitionPoint Point;
				Point.Id = PlacementId;
				Point.Role = Type->TransitionRole;
				Point.TypeId = Type->TypeId;
				Point.Location = Location;
				Point.Rotation = Rotation;
				Point.CellXZ = Placement.CellXZ;
				StagedTransitions.Add(MoveTemp(Point));
			}
		}

		FAssetRegistryModule::AssetCreated(RoomWorld);

		// Maps are not saved by hand but the same way the editor saves them on Ctrl+S:
		// that updates the package's LoadedPath and clears the housekeeping flags, without which
		// the level opens with the error "is unsaved and cannot be opened".
		const FString LevelFileName = FPackageName::LongPackageNameToFilename(
			LevelPackageName, FPackageName::GetMapPackageExtension());

		if (FEditorFileUtils::SaveLevel(RoomWorld->PersistentLevel, LevelFileName))
		{
			++OutReport.Levels;

			// Taken after the objects, not before: an object that had no number was just given
			// one, and the number is written into the level. Hashed with the old zero, the level
			// would look out of date on every pass.
			if (bRoomComplete)
			{
				NewLevelHashes.Add(Room.RoomId, ComputeLevelHash(*Asset, Room, RoomHash,
					RoomPlacements, Spawns, Library, LevelContext));
			}
		}
		else
		{
			++OutReport.FailedPackages;
		}

		// ------------------------------------------------------- manifest entry

		FMazeRoomEntry Entry;
		Entry.RoomId = Room.RoomId;
		Entry.Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(LevelPackageName + TEXT(".") + LevelName));
		Entry.WorldBounds = Room.WorldBounds;
		Entry.Neighbors = Room.Neighbors;
		Entry.EstimatedTriangles = RoomTriangles;
		StagedRooms.Add(MoveTemp(Entry));

		// ------------------------------------------------------- batch unload

		PendingUnload.AddUnique(LevelPackage);

		if (RoomsPerFlush > 0 && (OutReport.Rooms % RoomsPerFlush) == 0)
		{
			MazeExport::FlushBatch(PendingUnload, ManifestPackage, OutReport.FlushedPackages);
		}
	}

	// The tail of the last batch.
	MazeExport::FlushBatch(PendingUnload, ManifestPackage, OutReport.FlushedPackages);

	// Saved before the cancel check, and deliberately. The levels that were written are on disk
	// with ids baked into them; an id that exists in a level but not in the asset is worse than
	// no id at all, because the next export would hand that same number to something else.
	if (bIdsAssigned && Spawns)
	{
		Spawns->MarkPackageDirty();

		if (!MazeExport::SavePackageToDisk(Spawns->GetPackage(), Spawns,
			FPackageName::GetAssetPackageExtension()))
		{
			++OutReport.FailedPackages;
		}
	}

	if (bCancelled)
	{
		// The levels and meshes that were written stay on disk — they are correct, and the next
		// export will reuse them. Only the manifest is left as it was, because a manifest listing
		// part of a maze is worse than one listing the previous, complete version of it.
		UE_LOG(LogMazeForge, Warning,
			TEXT("Export cancelled after %d of %d rooms. The manifest was NOT rewritten — "
			     "run the export again to finish."),
			OutReport.Rooms, Asset->Rooms.Num());

		OutReport.Seconds = FPlatformTime::Seconds() - StartTime;
		return false;
	}

	Manifest->Rooms = MoveTemp(StagedRooms);
	Manifest->Transitions = MoveTemp(StagedTransitions);

	// Replaced whole: a room that produced no level this time must not keep a hash from the time
	// it did.
#if WITH_EDITOR
	Asset->Modify();
#endif
	Asset->ExportedLevelHashes = MoveTemp(NewLevelHashes);

	// After the manifest is right and before anything is reported. The retirement reads the
	// new slicing off the asset and the old one off the snapshot, so it has to sit between
	// the two — and it must not run at all if the export gave up, which the cancel above has
	// already returned for.
	const FMazeRetirementReport Retirement =
		FMazeRoomRetirement::Retire(Asset, PreviousRoomIds);

	OutReport.RetiredRooms = Retirement.Rooms;

	if (Manifest->Transitions.Num() > 0)
	{
		int32 Gates = 0;
		for (const FMazeTransitionPoint& Point : Manifest->Transitions)
		{
			Gates += (Point.Role == EMazeTransitionRole::Gate) ? 1 : 0;
		}

		UE_LOG(LogMazeForge, Log,
			TEXT("Export: %d transition points published — %d gates, %d entries. "
			     "Their ids are what the world graph links."),
			Manifest->Transitions.Num(), Gates, Manifest->Transitions.Num() - Gates);
	}
	Manifest->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Manifest);

	if (!MazeExport::SavePackageToDisk(ManifestPackage, Manifest,
		FPackageName::GetAssetPackageExtension()))
	{
		++OutReport.FailedPackages;
	}

	Asset->Manifest = Manifest;
	Asset->MarkPackageDirty();

	OutReport.Seconds = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogMazeForge, Log, TEXT("Export: %s"), *OutReport.ToString());
	UE_LOG(LogMazeForge, Log, TEXT("Levels in %s, manifest %s"), *LevelRoot, *ManifestPackageName);

	return OutReport.Levels + OutReport.ReusedLevels > 0;
}

#undef LOCTEXT_NAMESPACE
