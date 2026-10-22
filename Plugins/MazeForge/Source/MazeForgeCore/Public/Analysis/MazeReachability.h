#pragma once

#include "CoreMinimal.h"

struct FMazeGrid;

/**
 *  Where the player can get to. Two models:
 *
 *  Without gravity (the default) — any space he fits in, joined to the start in any direction.
 *  Up a shaft counts as reachable, on the understanding that a ladder, a lift or a rope will
 *  stand there by the time the level is played. What this finds is the pieces nothing could
 *  ever lead into: pockets sealed in mass, slits narrower than he is.
 *
 *  With gravity — the way he moves in a side view: walks along floors, falls from any height,
 *  steps or jumps up a few cells, and climbs only ladders. A fall that would kill him still
 *  counts: how high a drop is survivable is game design, not geometry.
 *
 *  Positions are the cell under the player's feet, in the drawing plane (X along the level, Z
 *  up); he fills AgentHeightCells cells above it. The grid's own solid cells and its closed
 *  world borders are walls; outside the grid is wall too.
 */
struct MAZEFORGECORE_API FMazeReachParams
{
	int32 AgentHeightCells = 2;

	/** How wide he is, in cells. At a 10 cm cell the stock capsule is nine of them, not one. */
	int32 AgentWidthCells = 1;

	/** Off — any space he fits in counts. On — up only by steps, jumps and climbables. */
	bool bGravity = false;

	/** How far up a step or a jump takes him, in cells. 0 — not at all: only ladders lead up. */
	int32 ClimbUpCells = 0;

	/** Where he arrives. Each is dropped to the first place he fits. */
	TArray<FIntPoint> Starts;

	/** Ladders and other climbables: XZ rectangles of cells, Min inclusive, Max exclusive. */
	TArray<FIntRect> Climbables;
};

/** A piece of the maze he can stand in but never gets to. */
struct MAZEFORGECORE_API FMazeReachIsland
{
	/** Bounds of its standing positions, inclusive. */
	FIntPoint Min = FIntPoint::ZeroValue;
	FIntPoint Max = FIntPoint::ZeroValue;

	/** Every standing position in it. */
	TArray<FIntPoint> Cells;
};

struct MAZEFORGECORE_API FMazeReachResult
{
	FIntPoint Size = FIntPoint::ZeroValue;

	/** He fits here and has something under him, or a ladder in his hands. */
	TBitArray<> Standable;

	/** Every position he gets to, mid-fall included. */
	TBitArray<> Reached;

	/** The starts actually used, after dropping each to where he fits. */
	TArray<FIntPoint> UsedStarts;

	int32 NumStandable = 0;
	int32 NumReachedStandable = 0;

	bool IsInside(int32 X, int32 Z) const { return X >= 0 && Z >= 0 && X < Size.X && Z < Size.Y; }
	int32 Index(int32 X, int32 Z) const { return Z * Size.X + X; }
	bool IsStandable(int32 X, int32 Z) const { return IsInside(X, Z) && Standable[Index(X, Z)]; }
	bool IsReached(int32 X, int32 Z) const { return IsInside(X, Z) && Reached[Index(X, Z)]; }
};

namespace MazeReachability
{
	/** Fills OutResult. With no starts given, starts from the lowest, then leftmost, standing spot. */
	MAZEFORGECORE_API void Solve(const FMazeGrid& Grid, const FMazeReachParams& Params, FMazeReachResult& OutResult);

	/** The unreached standing positions, grouped into connected pieces, largest first. */
	MAZEFORGECORE_API void FindIslands(const FMazeReachResult& Result, TArray<FMazeReachIsland>& OutIslands);

	/** What ConnectIsland did, so the caller can stand a ladder where one is needed. */
	struct FConnection
	{
		/** The way leads up, and a ladder must span [LadderBase.Y, LadderBase.Y + LadderSpan). */
		bool bNeedsLadder = false;
		FIntPoint LadderBase = FIntPoint::ZeroValue;
		int32 LadderSpan = 0;
		int32 CarvedCells = 0;
	};

	/**
	 *  Carves the shortest way from the reached part of the maze to one island: a tunnel the
	 *  player's height along the floor he is on, then a shaft ShaftWidth wide. Going down, the
	 *  shaft opens right above the island and he drops in. Going up, it rises beside the island
	 *  and the caller stands a ladder in it; with bAllowUp false that case is refused.
	 *
	 *  Mass is cleared through the whole depth; the painted back wall is left alone.
	 */
	MAZEFORGECORE_API bool ConnectIsland(FMazeGrid& Grid, const FMazeReachResult& Result,
	                                     const FMazeReachIsland& Island, int32 AgentHeightCells,
	                                     int32 AgentWidthCells, int32 ShaftWidth, bool bAllowUp,
	                                     FConnection& OutConnection);
}
