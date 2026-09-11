#pragma once

#include "CoreMinimal.h"
#include "Data/MazeSpawnTypes.h"

struct FMazeGrid;

/**
 *  Whether an object fits somewhere, and which anchor it fits by.
 *
 *  Its own file for one reason: three different callers ask the same question. The brush asks
 *  it under the cursor, the export asks it again to report placements the maze has grown out
 *  from under, and the generator will ask it of every candidate cell. Three copies of this rule
 *  would agree until somebody changed one — the exact failure the mesh and level names were
 *  pulled into a shared helper to avoid.
 */
namespace MazePlacement
{
	/** Every cell of the footprint is passable, and the contact edge of that anchor is mass. */
	MAZEFORGECORE_API bool Fits(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                            const FIntPoint& CellXZ, int32 SliceY, EMazeAnchorKind Anchor);

	/**
	 *  The first anchor the type allows that actually fits here, in order Floor, Ceiling, Wall,
	 *  Free. Returns false when none of them does.
	 *
	 *  Most specific first, because that is the order of intent: something that can stand and is
	 *  standing on a floor is on a floor, not floating in a spot that happens to be free.
	 */
	MAZEFORGECORE_API bool FindAnchor(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                                  const FIntPoint& CellXZ, int32 SliceY,
	                                  EMazeAnchorKind& OutAnchor);

	/**
	 *  Why an object does not fit here, in words, for a message.
	 *
	 *  Deliberately separate from Fits and deliberately slower. Fits has to be cheap because the
	 *  generator will ask it of every candidate cell; this is asked once, about the one placement
	 *  somebody is looking at. It exists because "does not fit" covers five different faults —
	 *  outside the grid, standing inside mass, nothing below, nothing above, nothing beside — and
	 *  the fix is different for every one of them. A refusal that will not say which is a refusal
	 *  you have to guess at, and guessing is what cost an evening the first time this fired.
	 *
	 *  Returns an empty string when the object does fit.
	 */
	MAZEFORGECORE_API FString DescribeMisfit(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                                         const FIntPoint& CellXZ, int32 SliceY);

	/** The anchor to record when nothing fits: the first one the type allows. Never fails. */
	MAZEFORGECORE_API EMazeAnchorKind PreferredAnchor(const FMazeObjectType& Type);

	/**
	 *  Rotation for a fresh placement, from the type's facing mode.
	 *
	 *  Seed rather than FMath::Rand: two runs of the generator over the same maze must produce
	 *  the same world, and a crate facing a different way is still a different world.
	 */
	MAZEFORGECORE_API FRotator ResolveRotation(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                                           const FIntPoint& CellXZ, int32 SliceY,
	                                           EMazeAnchorKind Anchor, int32 Seed);

	/** World transform of a placement, given its type. */
	MAZEFORGECORE_API FVector WorldLocation(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                                        const FMazePlacement& Placement);

	/** The depth slice a band is placed in: the middle of that band. */
	MAZEFORGECORE_API int32 BandSliceY(const FMazeGrid& Grid, EMazeDepthBand Band);
}
