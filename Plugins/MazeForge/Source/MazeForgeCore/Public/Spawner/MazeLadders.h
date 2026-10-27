#pragma once

#include "CoreMinimal.h"
#include "Data/MazeSpawnTypes.h"

struct FMazeGrid;
class UMazeObjectLibrary;

/**
 *  The free column a ladder can stand in, as the ladder sees it. Grid XZ cells.
 *
 *  A level is a row a ladder can start or end at: the floor under the column, and every exit
 *  above it — a corridor floor beside a shaft, or the slab around a hole in a ceiling.
 */
struct FMazeShaft
{
	/** The floor: the empty cell on the lowest mass of the column. */
	FIntPoint Bottom = FIntPoint::ZeroValue;

	/** Exit levels above the floor, ascending. */
	TArray<int32> Exits;

	/** The first exit, or INDEX_NONE. Where an automatic ladder from the floor ends. */
	int32 TopZ = INDEX_NONE;

	/** The first mass above the column. A ladder ends at this row at the highest. */
	int32 CeilingZ = INDEX_NONE;

	/** The hole at the first exit: its empty cells in the slab's top row, inclusive. */
	int32 HoleMinX = 0;
	int32 HoleMaxX = 0;
};

/**
 *  Where ladders go, and what they are made of.
 *
 *  One answer for the Ladder tool, Place All Ladders, the Classic generator and the export, so a
 *  ladder put down by any of them is the same pieces in the same places.
 */
namespace MazeLadders
{
	/**
	 *  The column through Cell: down to its floor, up to its ceiling, and the exits on the way.
	 *
	 *  An exit is a row where there is somewhere to step off right beside the ladder: going
	 *  outwards along the row over open air (within about 2 m), the first cell with mass under
	 *  it — at least 120 cm of mass, air under it, 150 cm of headroom (a floor, not an erosion
	 *  bump) — while the ladder's column there is in a gap bounded on both sides within 4 m (a
	 *  hole or a shaft, not open air beside a pillar). False only when Cell is mass.
	 */
	MAZEFORGECORE_API bool FindColumn(const FMazeGrid& Grid, int32 SliceY, const FIntPoint& Cell,
	                                  FMazeShaft& OutShaft);

	/** FindColumn, and true only when the column has an exit to climb to. */
	MAZEFORGECORE_API bool FindShaft(const FMazeGrid& Grid, int32 SliceY, const FIntPoint& Cell,
	                                 FMazeShaft& OutShaft);

	/** The highest level (the floor or an exit) at or below Z. */
	MAZEFORGECORE_API int32 LevelAtOrBelow(const FMazeShaft& Shaft, int32 Z);

	/**
	 *  Where a ladder ends when its top is picked at row Z: the highest exit above BottomZ and at
	 *  or below Z; without one, Z itself, kept under the ceiling. INDEX_NONE if that is not
	 *  above BottomZ.
	 */
	MAZEFORGECORE_API int32 SnapTop(const FMazeShaft& Shaft, int32 BottomZ, int32 Z);

	/**
	 *  One shaft per hole in the maze: every hole with a floor under it, the ladder in the column
	 *  closest to the middle of the hole.
	 */
	MAZEFORGECORE_API void FindAllShafts(const FMazeGrid& Grid, int32 SliceY, TArray<FMazeShaft>& Out);

	/** A ladder or a ladder's platform: a Climb type, or a type some ladder names as its top. */
	MAZEFORGECORE_API bool IsLadderPiece(const UMazeObjectLibrary* Library, const FMazePlacement& Placement);

	/**
	 *  Ladder pieces whose footprint crosses the rectangle [MinX, MaxX] x [MinZ, MaxZ],
	 *  inclusive. Indices into Placements, ascending.
	 */
	MAZEFORGECORE_API void FindPieces(const TArray<FMazePlacement>& Placements,
	                                  const UMazeObjectLibrary* Library, int32 MinX, int32 MaxX,
	                                  int32 MinZ, int32 MaxZ, TArray<int32>& OutIndices);

	/**
	 *  One ladder over column X, from level BottomZ to level TopZ — the whole length, one piece —
	 *  and, if its type names a Top Piece Type, the platform on TopZ. Climb Cells is set, so the
	 *  export sizes it by that. On the floor it stands by the Floor anchor; on another ladder's
	 *  platform it is Free, its pivot on that level. The variant is picked here. Band Play;
	 *  origin flags are the caller's. Returns the ladder type actually used.
	 */
	MAZEFORGECORE_API FName MakeLadder(const FMazeGrid& Grid, const UMazeObjectLibrary* Library,
	                                   FName LadderType, int32 X, int32 BottomZ, int32 TopZ,
	                                   bool bOnFloor, uint32 Seed, TArray<FMazePlacement>& Out);

	/**
	 *  Only the platform of a ladder of type LadderType (already resolved, not a variant to
	 *  pick from), its pivot on row TopZ above column X. False when the type names none.
	 */
	MAZEFORGECORE_API bool MakeTopPiece(const FMazeGrid& Grid, const UMazeObjectLibrary* Library,
	                                    FName LadderType, int32 X, int32 TopZ, FMazePlacement& Out);

	/**
	 *  What goes into the type's Climb Height Property for a climb this tall: the height over
	 *  Climb Unit Cm, rounded, plus Climb Extra Units. Never below 1.
	 */
	MAZEFORGECORE_API double ClimbValue(const FMazeObjectType& Type, double HeightCm);

	/** The hash a spot picks its variant by: same spot and seed, same variant. */
	MAZEFORGECORE_API uint32 SpotHash(const FIntPoint& Cell, uint32 Seed);
}
