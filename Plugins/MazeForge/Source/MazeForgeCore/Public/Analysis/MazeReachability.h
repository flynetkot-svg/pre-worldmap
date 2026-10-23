#pragma once

#include "CoreMinimal.h"

struct FMazeGrid;

/** A door or a key, for the check: where it stands and which channel it belongs to. */
struct MAZEFORGECORE_API FMazeReachLock
{
	/** Footprint in XZ cells, Min inclusive, Max exclusive. */
	FIntRect Rect;
	FName LockId;
};

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

	/**
	 *  Doors close the passage they stand in, floor to ceiling, until a key with their Lock Id
	 *  has been reached. Keys are picked up by touching them. Keys behind other doors count: the
	 *  check plays it out — reach what is open, pick up the keys there, open their doors, again
	 *  until nothing new opens.
	 */
	TArray<FMazeReachLock> Doors;
	TArray<FMazeReachLock> Keys;
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

	/** Reached before any key: everything beyond is behind a door. */
	TBitArray<> ReachedWithoutKeys;

	/** Per entry of Params.Keys: picked up at some point of the play-through. */
	TArray<bool> KeyReached;

	/**
	 *  Per entry of Params.Keys: how many rounds of doors had to open before it could be had.
	 *  0 — lying in the open; 1 — behind one door; INDEX_NONE — never.
	 */
	TArray<int32> KeyPass;

	/** Per position: steps from the start in the final round, INDEX_NONE where not reached. */
	TArray<int32> Distance;

	/** Per position: the round of doors it was first reached in; MAX_uint8 — never. */
	TArray<uint8> ReachPass;

	/** Per entry of Params.Doors: its key could be had, so it opens. */
	TArray<bool> DoorOpened;

	/** The world as it ended: mass, and the doors still shut. For testing where things fit. */
	TBitArray<> Solid;

	/** Free cells from each cell upwards, itself included, in that same world. */
	TArray<int32> FreeAbove;

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
