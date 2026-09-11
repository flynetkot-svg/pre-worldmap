#pragma once

#include "CoreMinimal.h"
#include "Data/MazeTypes.h"

struct FMazeGrid;
struct FMazeRoomDesc;
class UStaticMesh;
class UMazeBuildSettings;

/** What we are baking and for which depth band. */
struct FMazeBakeRequest
{
	const FMazeGrid* Grid = nullptr;
	const FMazeRoomDesc* Room = nullptr;
	const UMazeBuildSettings* Settings = nullptr;

	/** The depth band the cells are taken from. */
	EMazeDepthBand Band = EMazeDepthBand::Play;

	/** The full package name for the mesh being created. */
	FString PackageName;
	FString AssetName;
};

/** What came out of the bake. */
struct FMazeBakeResult
{
	/**
	 *  Something went wrong, as opposed to there being nothing to build.
	 *
	 *  Build() returns false for both, and the two could not be told apart — so a package that
	 *  could not be created read exactly like an empty foreground band, and the caller recorded
	 *  the room as successfully baked. Set this and the caller knows the difference.
	 */
	bool bFailed = false;

	UStaticMesh* Mesh = nullptr;
	int32 SourceCells = 0;
	int32 CulledCells = 0;
	int32 Quads = 0;
	int32 CollisionBoxes = 0;

	int32 Triangles() const { return Quads * 2; }
};

/**
 *  Builder for a room's geometry.
 *
 *  It sits behind an interface so that the way a mesh is built can be swapped out without
 *  touching the exporter: right now it is face culling, and later, if needed, greedy meshing
 *  with merging of coplanar quads can take its place.
 */
class IMazeMeshBuilder
{
public:
	virtual ~IMazeMeshBuilder() = default;

	virtual bool Build(const FMazeBakeRequest& Request, FMazeBakeResult& OutResult) = 0;

	virtual FString GetDisplayName() const = 0;
};
