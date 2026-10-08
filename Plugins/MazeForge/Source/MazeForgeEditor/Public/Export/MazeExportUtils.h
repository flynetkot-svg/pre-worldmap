#pragma once

#include "CoreMinimal.h"
#include "Data/MazeTypes.h"

class UMazeBuildSettings;
class UMazeGridAsset;
class UObject;
class UPackage;
class UWorld;

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

	/**
	 *  The asset name and package of a maze's manifest. The only place where they are defined.
	 *
	 *  The rule used to live inline in the exporter alone, and everything else reached the
	 *  manifest through the pointer stored on the grid asset. That pointer only exists once
	 *  the grid asset has been saved after an export — so a crash between the two, on a map
	 *  whose export had just succeeded, left a perfectly good manifest on disk that Attach
	 *  reported as "empty". Deriving the path is what lets every caller find it regardless.
	 */
	FString ManifestAssetName(const UMazeBuildSettings* Settings, const FString& MazeName);
	FString ManifestPackageName(const UMazeBuildSettings* Settings, const FString& MazeName);

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

	/** The world the editor is showing, or null outside the editor. */
	UWorld* EditorWorld();

	/**
	 *  Whether this maze may write its output: false when another grid asset with the same Maze
	 *  Name already owns it (see UMazeWorldManifest::BuiltFrom).
	 *
	 *  On refusal it logs an error and shows a dialog naming the other asset — Action goes into
	 *  both. Nothing is written by the caller after a refusal, so the other maze is untouched.
	 *  A manifest with no owner recorded, or whose owner no longer exists or now has a different
	 *  name, is free to take.
	 */
	bool EnsureOutputIsOurs(const UMazeGridAsset* Asset, const TCHAR* Action);

	/**
	 *  Clears the way for an asset named ObjectName in Package. Call after loading the package
	 *  and before looking for the asset in it.
	 *
	 *  A redirector sitting under that name is moved aside: Move Stale Rooms To Deprecated, or
	 *  any rename, leaves one at the old path while something still references it — the
	 *  persistent level does, for a room level. When the room comes back under the same name,
	 *  creating it on top of the redirector is a name clash the engine stops on with a check.
	 *  The rebuilt asset takes the path over; the moved copy in Deprecated stays as it was.
	 *
	 *  Returns false only when something else that is not of ExpectedClass holds the name —
	 *  the caller must then skip the asset rather than create it.
	 */
	bool ClearNameFor(UPackage* Package, const FString& ObjectName, const UClass* ExpectedClass);
}
