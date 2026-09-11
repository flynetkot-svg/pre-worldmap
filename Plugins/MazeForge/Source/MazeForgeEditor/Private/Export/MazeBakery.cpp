#include "Export/MazeBakery.h"

#include "Assets/MazeBuildSettings.h"
#include "Assets/MazeGridAsset.h"
#include "Data/MazeGrid.h"
#include "Data/MazeRoomDesc.h"
#include "Engine/StaticMesh.h"
#include "Export/MazeExportUtils.h"
#include "Export/MazeMeshBuilder_Faces.h"
#include "MazeForgeCore.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "MazeForgeEditor"

FString FMazeBakeReport::ToString() const
{
	const double CullPercent = (SourceCells > 0)
		? 100.0 * CulledCells / SourceCells
		: 0.0;

	return FString::Printf(
		TEXT("rooms %d (of them unchanged %d), meshes %d, cells %d (hidden ones culled %d, %.0f%%), ")
		TEXT("quads %d, triangles %d, collision boxes %d; ")
		TEXT("write failures %d; packages unloaded %d; in %.2f s"),
		Rooms, SkippedRooms, Meshes, SourceCells, CulledCells, CullPercent,
		Quads, Triangles(), CollisionBoxes, FailedPackages, FlushedPackages, Seconds);
}

bool FMazeBakery::BakeRooms(UMazeGridAsset* Asset, FMazeBakeReport& OutReport, bool bForceAll)
{
	if (!Asset)
	{
		return false;
	}

	if (Asset->Rooms.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Bake: there are no rooms. Press Slice Into Rooms first."));
		return false;
	}

	const UMazeBuildSettings* Settings = Asset->BuildSettings.LoadSynchronous();
	const FString MazeName = Asset->GetSafeMazeName();
	const FString MeshRoot = MazeExport::ScopedRoot(
		Settings ? Settings->MeshPackageRoot : TEXT("/Game/MazeForge/Meshes"), MazeName);
	const bool bSplitByBand = Settings ? Settings->bSplitByDepthBand : true;
	const int32 RoomsPerFlush = Settings ? Settings->RoomsPerFlush : 16;

	const double StartTime = FPlatformTime::Seconds();

	FScopedSlowTask SlowTask(static_cast<float>(Asset->Rooms.Num()),
		LOCTEXT("BakingRooms", "MazeForge: baking room meshes"));
	SlowTask.MakeDialog(true);

	FMazeMeshBuilder_Faces Builder;

	const EMazeDepthBand AllBands[3] = {
		EMazeDepthBand::Background, EMazeDepthBand::Play, EMazeDepthBand::Foreground
	};

	// Packages of saved meshes waiting to be unloaded. They pile up until the end of a batch.
	TArray<UPackage*> PendingUnload;

	bool bCancelled = false;

	// Rooms that came through this pass intact. Written to the asset at the end rather than as we
	// go, so that a cancelled bake cannot leave a room marked fresh when it was never rebuilt.
	TMap<FName, int64> NewHashes;

	for (const FMazeRoomDesc& Room : Asset->Rooms)
	{
		if (SlowTask.ShouldCancel())
		{
			UE_LOG(LogMazeForge, Warning, TEXT("Bake cancelled by the user."));
			bCancelled = true;
			break;
		}
		SlowTask.EnterProgressFrame(1.0f);

		++OutReport.Rooms;

		// The whole point of the incremental bake: cutting one hole in one room used to mean
		// rebuilding all 208. The hash covers the room's cells and one cell beyond its bounds —
		// see UMazeGridAsset::ComputeRoomHash — so a change on the far side of a seam is caught
		// by both rooms it affects.
		const int64 Hash = Asset->ComputeRoomHash(Room);

		// The hash is the whole test, deliberately. Checking the files on disk as well was tried
		// and removed: a room whose every band is empty produces no file at all, so "no file" and
		// "not baked" are not the same question, and answering one with the other left the bake
		// and the status line permanently disagreeing about such rooms. What a hash cannot see —
		// meshes deleted or edited behind the plugin's back — is what Force Full Rebake is for,
		// and the export rebuilds any band it fails to load regardless.
		if (!bForceAll)
		{
			const int64* Baked = Asset->BakedRoomHashes.Find(Room.RoomId);
			if (Baked && *Baked == Hash)
			{
				++OutReport.SkippedRooms;
				NewHashes.Add(Room.RoomId, Hash);
				continue;
			}
		}

		bool bRoomComplete = true;
		const int32 BandCount = bSplitByBand ? 3 : 1;
		for (int32 Index = 0; Index < BandCount; ++Index)
		{
			FMazeBakeRequest Request;
			Request.Grid = &Asset->Grid;
			Request.Room = &Room;
			Request.Settings = Settings;
			Request.Band = bSplitByBand ? AllBands[Index] : EMazeDepthBand::Play;
			Request.AssetName = MazeExport::MeshAssetName(
				MazeName, Room.RoomId, Request.Band, bSplitByBand);
			Request.PackageName = MeshRoot / Request.AssetName;

			FMazeBakeResult Result;
			const bool bBuilt = Builder.Build(Request, Result) && Result.Mesh;

			// We count cells and culling even for empty bands — otherwise the statistics lie.
			OutReport.SourceCells += Result.SourceCells;
			OutReport.CulledCells += Result.CulledCells;

			if (!bBuilt)
			{
				// A band with nothing in it produces no mesh, and that is not a failure. A band
				// that failed to build is, and the room must not be recorded as baked.
				if (Result.bFailed)
				{
					++OutReport.FailedPackages;
					bRoomComplete = false;
				}

				continue;
			}

			// The bake used to leave the mesh dirty in memory, and the export would rebuild it
			// from scratch anyway. We write it to disk — then all the export has left to do is
			// assemble the levels.
			if (!MazeExport::SavePackageToDisk(Result.Mesh->GetPackage(), Result.Mesh,
				FPackageName::GetAssetPackageExtension()))
			{
				++OutReport.FailedPackages;
				bRoomComplete = false;
				continue;
			}

			PendingUnload.AddUnique(Result.Mesh->GetPackage());

			++OutReport.Meshes;
			OutReport.Quads += Result.Quads;
			OutReport.CollisionBoxes += Result.CollisionBoxes;
		}

		// Recorded only if every band of this room reached the disk. A room whose write failed
		// must come back on the next pass, not be taken for fresh.
		if (bRoomComplete)
		{
			NewHashes.Add(Room.RoomId, Hash);
		}

		if (RoomsPerFlush > 0 && (OutReport.Rooms % RoomsPerFlush) == 0)
		{
			MazeExport::FlushBatch(PendingUnload, nullptr, OutReport.FlushedPackages);
		}
	}

	// The tail of the last batch.
	MazeExport::FlushBatch(PendingUnload, nullptr, OutReport.FlushedPackages);

	OutReport.Seconds = FPlatformTime::Seconds() - StartTime;

	// Per room, not per pass. A cancelled bake used to invalidate everything, including the rooms
	// it had already finished; now what got baked stays baked and only the rest comes back next
	// time. Rooms the pass never reached keep whatever they had — which may be nothing.
	if (NewHashes.Num() > 0)
	{
		// Modify() BEFORE the write — that is the whole contract: it records the state as it is
		// now, so an undo has something to go back to. The hashes are saved state, and a Ctrl+Z
		// on the paint stroke that preceded the bake would otherwise roll the asset back past a
		// bake it never knew about, discarding the record but not the files on disk.
#if WITH_EDITOR
		Asset->Modify();
#endif

		for (const TPair<FName, int64>& Pair : NewHashes)
		{
			Asset->BakedRoomHashes.Add(Pair.Key, Pair.Value);
		}

		Asset->MarkPackageDirty();
	}

	// The status line counts baked rooms through a memo, and the hashes just moved under it.
	Asset->InvalidateBakedRoomCache();

	UE_LOG(LogMazeForge, Log, TEXT("Mesh bake: %s. %d of %d rooms are up to date%s"),
		*OutReport.ToString(), NewHashes.Num(), Asset->Rooms.Num(),
		bCancelled ? TEXT(" — the pass was cancelled, the rest will be rebuilt next time.")
		           : TEXT("."));

	UE_LOG(LogMazeForge, Log, TEXT("Meshes written to %s."), *MeshRoot);

	// True means "the maze is baked", not "some geometry was produced". A pass that found every
	// room already up to date builds nothing at all, and that is the success case.
	return !bCancelled && OutReport.FailedPackages == 0;
}

#undef LOCTEXT_NAMESPACE
