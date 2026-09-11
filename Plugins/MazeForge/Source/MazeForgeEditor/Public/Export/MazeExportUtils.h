#pragma once

#include "CoreMinimal.h"
#include "Data/MazeTypes.h"

class UObject;
class UPackage;

/**
 *  Shared mechanics of the build pipeline: asset names, writing to disk, batched unloading.
 *
 *  This was pulled out of the exporter not for tidiness. The mesh bake (FMazeBakery) and the
 *  level export (FMazeLevelExporter) are two different steps, but they need THE SAME naming
 *  rules: otherwise the second step will not find what the first one baked and will build it
 *  again. While this lived in the exporter's private namespace, the mesh name existed in two
 *  copies, and any divergence would have cost a full recomputation of the geometry.
 */
namespace MazeExport
{
	const TCHAR* BandSuffix(EMazeDepthBand Band);

	/**
	 *  A package root with the maze's own compartment appended, or the root unchanged when the
	 *  maze is unnamed. Every path the build writes to goes through this.
	 */
	FString ScopedRoot(const FString& Root, const FString& MazeName);

	/** The asset name of a room mesh. The only place where it is defined. */
	FString MeshAssetName(const FString& MazeName, FName RoomId, EMazeDepthBand Band,
	                      bool bSplitByBand);

	/**
	 *  The asset name of a room level. Also the only place where it is defined — it used to be
	 *  built inline in two, which is the same divergence this file exists to prevent.
	 */
	FString LevelAssetName(const FString& MazeName, FName RoomId);

	/** Strips the edge slashes: a leading one makes a nameless ghost folder in the Outliner. */
	FString CleanFolder(FString Folder);

	/** The Outliner folder this maze puts its rooms under. Shared by the export and the detach. */
	FString OutlinerRoot(const FString& RootSetting, const FString& MazeName);

	/** The full path to the mesh object: /Path/SM_Name.SM_Name — what LoadObject expects. */
	FString MeshObjectPath(const FString& MeshRoot, const FString& AssetName);

	bool SavePackageToDisk(UPackage* Package, UObject* Asset, const FString& Extension);

	/**
	 *  Unloads packages that are already written to disk and collects garbage.
	 *
	 *  Without this the bake keeps everything in memory at once: on a 208-room maze that is
	 *  624 meshes and 208 worlds with live render resources. The editor does not run out of
	 *  RAM but of the reserved RHI address space —
	 *  "Total reserved resource allocated virtual size exceeds the budget" — and dies with an
	 *  exception on the render thread after everything has already been saved successfully.
	 *
	 *  Unloading loses nothing: it is all on disk already, and the references go by path.
	 */
	void FlushBatch(TArray<UPackage*>& InOutPackages, UPackage* KeepPackage,
	                int32& InOutFlushedCount);
}
