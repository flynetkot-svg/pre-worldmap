#pragma once

#include "CoreMinimal.h"

struct FMazeReachResult;

/**
 *  The shape of a maze as the player walks it: a main path, the branches off it, the branches
 *  off those, and where they end.
 *
 *  Built from the reachability solve, so it walks the way the check does — agent size, gravity
 *  mode, doors opening as their keys are found. Distances from the start are cut into bands a
 *  few cells deep; each connected piece of a band is a node, and a node leads to the pieces of
 *  the next band it touches. Where the front of the walk splits, a branch starts; where a piece
 *  leads nowhere, a dead end is; where two fronts meet again, a loop closes. Corridors one cell
 *  or ten cells thick, halls and shafts all come out the same way, which is what lets it read
 *  any generator's maze — and a hand-drawn one.
 */
struct MAZEFORGECORE_API FMazePathNode
{
	/** Centre of its positions, in cells (X along the level, Y up), feet level. */
	FVector2f Centre = FVector2f::ZeroVector;
	int32 Band = 0;
	int32 Cells = 0;
	TArray<int32> Next;
	TArray<int32> Prev;

	bool bMain = false;

	/** Which branch it belongs to; INDEX_NONE on the main path. */
	int32 Branch = INDEX_NONE;

	/** 1 — a branch off the main path, 2 — off a branch, and so on. 0 on the main path. */
	int32 Level = 0;
};

struct MAZEFORGECORE_API FMazePathStats
{
	FIntPoint Start = FIntPoint::ZeroValue;
	FIntPoint Exit = FIntPoint::ZeroValue;

	/** The exit is a Gate point; false — the farthest place the player gets to. */
	bool bExitFromGate = false;

	int32 MainPathCells = 0;
	int32 Branches = 0;
	int32 DeadEnds = 0;

	/** Dead ends by level: [0] off the main path itself, [1] off a branch, … */
	TArray<int32> DeadEndsPerLevel;

	/** Places on the main path where a branch leaves it. */
	int32 MainJunctions = 0;
	int32 Loops = 0;

	/** Cells from where a branch leaves the main path to its farthest end. */
	int32 MaxBranchDepth = 0;
	float AvgBranchDepth = 0.0f;

	/** Deepest level of branch-off-a-branch. */
	int32 MaxNesting = 0;

	/** 0–100: branches per main-path length, their depth, their nesting and loops, in one number. */
	int32 Confusion = 0;

	FString ToString() const;
};

struct MAZEFORGECORE_API FMazePathGraph
{
	TArray<FMazePathNode> Nodes;
	int32 StartNode = INDEX_NONE;
	int32 ExitNode = INDEX_NONE;
	int32 BandCells = 2;
	FMazePathStats Stats;

	/** Per position of the solve: the node it belongs to, INDEX_NONE where not reached. */
	TArray<int32> NodeOf;
};

namespace MazePaths
{
	/**
	 *  Builds the graph from a solve. Exits are Gate points (any number; the farthest reached is
	 *  taken); none, or none reached — the farthest place to stand. BandCells — how deep a slice
	 *  of distance one node covers: about the player's width keeps a corridor one node per band.
	 *  False when nothing was reached.
	 */
	MAZEFORGECORE_API bool Build(const FMazeReachResult& Result, const TArray<FIntPoint>& Exits,
	                             int32 BandCells, FMazePathGraph& OutGraph);
}
