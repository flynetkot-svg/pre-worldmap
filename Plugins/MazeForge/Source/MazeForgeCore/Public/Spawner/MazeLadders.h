#pragma once

#include "CoreMinimal.h"
#include "Data/MazeSpawnTypes.h"

struct FMazeGrid;
class UMazeObjectLibrary;

/** A hole between two floors, as a ladder sees it. Grid XZ cells. */
struct FMazeShaft
{
	/** The empty cell on the lower floor the ladder stands in. */
	FIntPoint Bottom = FIntPoint::ZeroValue;

	/** The upper floor's level: the first empty row above the slab the hole goes through. */
	int32 TopZ = INDEX_NONE;
};

/**
 *  Where a ladder goes, and what it is made of.
 *
 *  One answer for the Ladder tool, the Classic generator and Fix Reachability, so that a ladder
 *  put down by any of them is the same two pieces in the same two places.
 */
namespace MazeLadders
{
	/**
	 *  The shaft through Cell: down to the floor under it, up through the hole above it.
	 *
	 *  The top is the first row, going up, where there is something to step off onto: two cells
	 *  of mass side by side with air under them — a slab, not a step in the lower floor. False
	 *  when Cell is mass, or the column above it hits mass first (a ceiling with no hole).
	 */
	MAZEFORGECORE_API bool FindShaft(const FMazeGrid& Grid, int32 SliceY, const FIntPoint& Cell,
	                                 FMazeShaft& OutShaft);

	/**
	 *  One ladder: the ladder on the lower floor and, if its type names a Top Piece Type, the
	 *  platform on the upper floor's level. The variant is picked here (see
	 *  UMazeObjectLibrary::PickVariant). Band Play; origin flags are the caller's to set.
	 *  Returns the ladder type actually used.
	 */
	MAZEFORGECORE_API FName MakeLadder(const FMazeGrid& Grid, const UMazeObjectLibrary* Library,
	                                   FName LadderType, const FMazeShaft& Shaft, uint32 Seed,
	                                   TArray<FMazePlacement>& Out);

	/**
	 *  Only the platform of a ladder of type LadderType (already resolved, not a variant to
	 *  pick from), its pivot on row TopZ above column X. False when the type names none.
	 */
	MAZEFORGECORE_API bool MakeTopPiece(const FMazeGrid& Grid, const UMazeObjectLibrary* Library,
	                                    FName LadderType, int32 X, int32 TopZ, FMazePlacement& Out);

	/** The hash a spot picks its variant by: same spot and seed, same variant. */
	MAZEFORGECORE_API uint32 SpotHash(const FIntPoint& Cell, uint32 Seed);
}
