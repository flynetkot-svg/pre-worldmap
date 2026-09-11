#pragma once

#include "CoreMinimal.h"

class UMazeGridAsset;

/** The summary report of a bake — what a room's budget is measured by. */
struct FMazeBakeReport
{
	int32 Rooms = 0;

	/** Rooms left alone because nothing about them changed since the last bake. */
	int32 SkippedRooms = 0;

	int32 Meshes = 0;
	int32 SourceCells = 0;
	int32 CulledCells = 0;
	int32 Quads = 0;
	int32 CollisionBoxes = 0;

	/** Failures writing to disk. The export will rebuild any mesh that was not saved. */
	int32 FailedPackages = 0;

	/** How many packages were unloaded from memory over the course of the bake. */
	int32 FlushedPackages = 0;

	double Seconds = 0.0;

	int32 Triangles() const { return Quads * 2; }
	FString ToString() const;
};

/**
 *  Step 4 of the pipeline: baking the meshes of every room and writing them to disk.
 *
 *  The meshes used to be built twice — here and once more during the level export. Now this is
 *  one real step: the result is saved, and the export picks it up as long as the grid has not
 *  been edited. That is also where the batched unloading comes from — the editor does not
 *  survive 624 meshes in memory at once.
 */
class FMazeBakery
{
public:
	/**
	 *  @param bForceAll  rebuild every room, ignoring the per-room hashes. The escape hatch for
	 *                    when the meshes on disk were changed or deleted behind the plugin's back.
	 */
	static bool BakeRooms(UMazeGridAsset* Asset, FMazeBakeReport& OutReport, bool bForceAll = false);
};
