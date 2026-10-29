#pragma once

#include "CoreMinimal.h"
#include "Data/MazeSpawnTypes.h"

struct FMazeGrid;
class UMazeObjectLibrary;

/** Where a rope goes: the gap cells it spans and the ledges at its ends. */
struct FMazeRopeSpan
{
	/** The row the player runs in; the rope lies along its bottom. */
	int32 Level = 0;

	/** The first and last cells of the gap, inclusive. */
	int32 FirstX = 0;
	int32 LastX = -1;

	/** A ledge to stand an anchor on, beside each end. A rope may also end at a wall. */
	bool bLeftLedge = false;
	bool bRightLedge = false;

	int32 NumCells() const { return LastX - FirstX + 1; }
};

/**
 *  Ropes, for the Rope tool and the export alike.
 *
 *  A rope is a placement of a type with Rope set and Span Cells: from its cell to the right, along
 *  the bottom of its cell row — level with the ledges either side. Its ends are tied to a ledge or
 *  to a wall; the anchors on the ledges are placements of their own.
 */
namespace MazeRopes
{
	MAZEFORGECORE_API bool IsRope(const UMazeObjectLibrary* Library, const FMazePlacement& Placement);

	/**
	 *  The level a rope clicked at Cell would run at: the floor at or below it, within Tolerance
	 *  cells — or, from inside mass, the first free row above. False if there is none near.
	 */
	MAZEFORGECORE_API bool PickLevel(const FMazeGrid& Grid, int32 SliceY, const FIntPoint& Cell,
	                                 int32 Tolerance, int32& OutLevel);

	/**
	 *  The rope between columns XA and XB at Level: the ledge cells at the ends are trimmed off,
	 *  leaving the gap. False, with the reason, if mass stands in the way, there is no gap, or an
	 *  end hangs in the air with neither a ledge nor a wall to tie it to.
	 */
	MAZEFORGECORE_API bool FitSpan(const FMazeGrid& Grid, int32 SliceY, int32 Level, int32 XA, int32 XB,
	                               FMazeRopeSpan& Out, FString* OutReason = nullptr);

	/**
	 *  The rope between two clicks: FitSpan tried at every level within Tolerance of either
	 *  click's level, and the best taken — ledges at both ends first, then the nearest to the
	 *  first click. A click a row off, beside the ledge instead of on it, still finds the ledges.
	 *  False, with the reason at the first click's level, if no level works.
	 */
	MAZEFORGECORE_API bool FindSpan(const FMazeGrid& Grid, int32 SliceY, const FIntPoint& ClickA, int32 LevelA,
	                                const FIntPoint& ClickB, int32 Tolerance, FMazeRopeSpan& Out,
	                                FString* OutReason = nullptr);

	/** The cells a rope covers, as a rectangle: its row, Span Cells wide. Max exclusive. */
	MAZEFORGECORE_API FIntRect Rect(const FMazePlacement& Rope);
}
