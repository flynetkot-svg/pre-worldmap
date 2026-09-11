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
#include "Export/MazeMeshBuilder_Faces.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
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

FString FMazeExportReport::ToString() const
{
	return FString::Printf(
		TEXT("rooms %d, levels %d, meshes %d (of them reused %d), ")
		TEXT("triangles %d, collision boxes %d; ")
		TEXT("objects %d (re-anchored %d, skipped %d, unknown type %d); ")
		TEXT("generated actors replaced %d, user actors preserved %d; ")
		TEXT("save failures %d; packages unloaded %d; in %.2f s"),
		Rooms, Levels, Meshes, ReusedMeshes, Triangles, CollisionBoxes,
		Objects, ReanchoredObjects, SkippedObjects, OrphanObjects,
		ReplacedActors, PreservedActors, FailedPackages, FlushedPackages, Seconds);
}

bool FMazeLevelExporter::ExportRooms(UMazeGridAsset* Asset, FMazeExportReport& OutReport)
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

			const FMazeRoomDesc* Owner = Asset->Rooms.FindByPredicate(
				[&Cell](const FMazeRoomDesc& Candidate)
				{
					return Candidate.ContainsXZ(Cell.X, Cell.Y);
				});

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
	FString ManifestName = Settings ? Settings->ManifestAssetName : FString();
	ManifestName.TrimStartAndEndInline();
	if (ManifestName.IsEmpty())
	{
		ManifestName = TEXT("DA_MazeWorldManifest");
	}

	// The manifest is the one asset a maze has exactly one of, so it collides even when the rooms
	// do not. It lives in the compartment as well, and says which maze it is in its own name.
	if (!MazeName.IsEmpty())
	{
		ManifestName += TEXT("_") + MazeName;
	}

	// An empty ManifestPackageRoot means "next to the levels": most projects have a single
	// maze, and a separate folder for one asset only gets in the way.
	FString ManifestRoot = Settings ? Settings->ManifestPackageRoot : FString();
	ManifestRoot.TrimStartAndEndInline();
	while (ManifestRoot.RemoveFromEnd(TEXT("/"))) {}
	if (ManifestRoot.IsEmpty())
	{
		ManifestRoot = LevelRoot;
	}

	const FString ManifestPackageName = ManifestRoot / ManifestName;
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

		// ------------------------------------------------------- room meshes

		TArray<UStaticMesh*> RoomMeshes;
		int32 RoomTriangles = 0;

		// Computed once and reused below: the hash walks the room's cells plus a one-cell skirt,
		// and doing that twice per room would be the most expensive part of the export.
		const int64 RoomHash = Asset->ComputeRoomHash(Room);
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

		const FString LevelName = MazeExport::LevelAssetName(MazeName, Room.RoomId);
		const FString LevelPackageName = LevelRoot / LevelName;

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

			// A re-anchored object keeps neither its rotation nor the reason for it: the stored
			// angle was resolved against the anchor it has just lost.
			const FRotator Rotation = bReanchored
				? MazePlacement::ResolveRotation(Grid, *Type, Placement.CellXZ, SliceY, PlacedAnchor,
					Grid.NumCells())
				: Placement.Rotation;

			const FVector Location = MazePlacement::WorldLocation(Grid, *Type, Placement);

			AActor* Object = ObjectClass
				? RoomWorld->SpawnActor<AActor>(ObjectClass, FTransform(Rotation, Location),
					SpawnParams)
				: nullptr;

			if (!Object && ObjectClass)
			{
				++OutReport.SkippedObjects;
				UE_LOG(LogMazeForge, Warning,
					TEXT("Export: could not spawn '%s' at X %d Z %d."),
					*Type->TypeId.ToString(), Placement.CellXZ.X, Placement.CellXZ.Y);
				continue;
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

	return OutReport.Levels > 0;
}

#undef LOCTEXT_NAMESPACE
