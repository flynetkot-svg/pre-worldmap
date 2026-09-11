#pragma once

#include "CoreMinimal.h"
#include "Data/MazeTypes.h"

struct FMazeGrid;

/**
 *  Dense working buffer of the layout in the XZ plane.
 *
 *  The generator computes the solution here, not in FMazeGrid. The reason is simple:
 *  the grid is sparse (a TMap), while the algorithm needs millions of neighbour lookups
 *  and a flood fill. In a flat array that is index arithmetic, in a TMap it is a hash on
 *  every touch.
 *
 *  There is deliberately no depth here. A maze is a two-dimensional solution stretched
 *  along Y; a shaft, a passage and a slab are the same on every depth slice. That is why
 *  connectivity is computed in XZ too: the third dimension adds not a single route.
 *
 *  The key point: connectivity is computed for AN AGENT WITH A SIZE, not for a point. The
 *  player takes up several cells in height, and a one-cell-tall passage is a wall to them,
 *  no matter how much success the flood fill reports.
 */
struct FMazeLayout2D
{
	explicit FMazeLayout2D(const FIntPoint& InSize);

	FIntPoint Size = FIntPoint::ZeroValue;

	int32 Width() const { return Size.X; }
	int32 Height() const { return Size.Y; }

	bool IsInside(int32 X, int32 Z) const
	{
		return X >= 0 && X < Size.X && Z >= 0 && Z < Size.Y;
	}

	int32 Index(int32 X, int32 Z) const { return Z * Size.X + X; }

	EMazeCellType Get(int32 X, int32 Z) const
	{
		return IsInside(X, Z) ? Cells[Index(X, Z)] : EMazeCellType::Solid;
	}

	void Set(int32 X, int32 Z, EMazeCellType Type)
	{
		if (IsInside(X, Z))
		{
			Cells[Index(X, Z)] = Type;
		}
	}

	/** Whether a single cell is free. Past the edge of the buffer — it is not. */
	bool IsFree(int32 X, int32 Z) const
	{
		const EMazeCellType Type = Get(X, Z);
		return Type != EMazeCellType::Solid && Type != EMazeCellType::Floor;
	}

	/**
	 *  Whether an agent standing with its feet at (X, Z) fits.
	 *
	 *  Support under the feet is not required: the player is sometimes falling, and
	 *  "reachable in at least one direction" includes "you can fall in here from above".
	 */
	bool CanStand(int32 X, int32 Z, int32 AgentHeight) const;

	void Fill(EMazeCellType Type);

	/** A rectangle. Min inclusive, Max exclusive. Clamped to the buffer. */
	void FillRect(const FIntPoint& Min, const FIntPoint& Max, EMazeCellType Type);

	/** The same, but does not touch already free cells — so a slab does not plug a passage. */
	void FillRectIfSolid(const FIntPoint& Min, const FIntPoint& Max, EMazeCellType Type);

	/**
	 *  Marks the agent positions (the cell under the feet) reachable from Start.
	 *
	 *  Returns their count. OutStandable has the same size as the buffer.
	 */
	int32 FloodFill(const FIntPoint& Start, int32 AgentHeight, TBitArray<>& OutStandable) const;

	/**
	 *  The cells occupied by the agent in at least one reachable position.
	 *
	 *  This is the living space of the maze. It must not be filled in under any
	 *  circumstances, otherwise we wall up a piece of passable volume.
	 */
	void BuildOccupied(const TBitArray<>& Standable, int32 AgentHeight, TBitArray<>& OutOccupied) const;

	/**
	 *  Isolated cavities — groups of agent-reachable positions that did not end up in
	 *  Standable. Each is returned as one of its cells: that is enough to carve through.
	 */
	void FindIslands(const TBitArray<>& Standable, int32 AgentHeight,
	                 TArray<FIntPoint>& OutIslandSeeds, TArray<int32>& OutIslandSizes) const;

	/**
	 *  Fills with Solid the cavity containing Seed, without entering Forbidden.
	 *
	 *  Without the limiter the fill would leak through a one-cell gap straight into the
	 *  living maze: to the fill that gap is passable, to the player it is not.
	 */
	int32 FloodSolidBounded(const FIntPoint& Seed, const TBitArray<>& Forbidden);

	/** The nearest reachable cell in a straight line. INDEX_NONE in X if none was found. */
	FIntPoint FindNearestReachable(const FIntPoint& From, const TBitArray<>& Standable) const;

private:
	TArray<EMazeCellType> Cells;
};

/**
 *  Stretches the flat solution along the depth and writes it into the grid.
 *
 *  Shared by all the generators: both the random one and the image one solve the problem
 *  in XZ, and the extrusion along Y is the same for both. Keeping two copies of this code
 *  is a sure way to fix one and forget about the other.
 */
void MazeWriteLayoutToGrid(const FMazeLayout2D& Layout, FMazeGrid& InOutGrid);
