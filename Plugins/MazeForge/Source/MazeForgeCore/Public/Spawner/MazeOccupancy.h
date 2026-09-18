#pragma once

#include "CoreMinimal.h"

struct FMazeObjectType;
struct FMazePlacement;
class UMazeObjectLibrary;

/**
 *  Which cells already have something standing in them.
 *
 *  The piece the placement rules deliberately do not have. MazePlacement::Fits asks the grid
 *  about mass and nothing else, which is right for the brush — a hand places one object at a
 *  time and an eye catches the overlap — and useless for a generator, which places blind and
 *  would happily stack ten crates in one cell with every rule satisfied.
 *
 *  Kept out of FMazeGrid on purpose. The grid is what the maze is made of and is saved with
 *  the asset; this is scratch built at the start of one generation pass and thrown away at the
 *  end of it. Storing it would mean keeping it in step with every brush click for ever, to
 *  serve a reader that exists for a fraction of a second.
 *
 *  Cells only, no depth. Objects live in one slice of their band, and two objects in the same
 *  XZ cell in different bands is not a case the side view can tell apart anyway.
 */
struct MAZEFORGECORE_API FMazeOccupancy
{
	/** Marks a footprint rectangle as taken. Max is exclusive, as everywhere else. */
	void Add(const FIntPoint& MinXZ, const FIntPoint& ExtentXZ);

	/** Every placement already in the asset, footprints read from the library. */
	void AddPlacements(const UMazeObjectLibrary* Library, const TArray<FMazePlacement>& Placements);

	/** True when no cell of this footprint is taken. */
	bool IsFree(const FIntPoint& MinXZ, const FIntPoint& ExtentXZ) const;

	/**
	 *  True when the footprint is free AND nothing stands within SpacingCells of it.
	 *
	 *  Spacing is what keeps a generated room from looking poured rather than placed: three
	 *  crates in a row touching each other read as one lump. Zero spacing is the same question
	 *  as IsFree.
	 */
	bool IsClearOf(const FIntPoint& MinXZ, const FIntPoint& ExtentXZ, int32 SpacingCells) const;

	int32 Num() const { return Cells.Num(); }

	void Reset() { Cells.Reset(); }

private:
	TSet<FIntPoint> Cells;
};
