#include "Spawner/MazeOccupancy.h"

#include "Assets/MazeObjectLibrary.h"
#include "Data/MazeSpawnTypes.h"

namespace
{
	/** Extents are a count of cells, so anything below one is one. Matches SafeFootprint. */
	FIntPoint SafeExtent(const FIntPoint& ExtentXZ)
	{
		return FIntPoint(FMath::Max(1, ExtentXZ.X), FMath::Max(1, ExtentXZ.Y));
	}
}

void FMazeOccupancy::Add(const FIntPoint& MinXZ, const FIntPoint& ExtentXZ)
{
	const FIntPoint Size = SafeExtent(ExtentXZ);

	Cells.Reserve(Cells.Num() + Size.X * Size.Y);

	for (int32 X = MinXZ.X; X < MinXZ.X + Size.X; ++X)
	{
		for (int32 Z = MinXZ.Y; Z < MinXZ.Y + Size.Y; ++Z)
		{
			Cells.Add(FIntPoint(X, Z));
		}
	}
}

void FMazeOccupancy::AddPlacements(const UMazeObjectLibrary* Library,
                                   const TArray<FMazePlacement>& Placements)
{
	for (const FMazePlacement& Placement : Placements)
	{
		// One cell when the type is unknown, rather than nothing. A placement whose type was
		// deleted from the library still stands in the world until somebody erases it, and
		// treating it as thin air would let the generator put a crate straight through it.
		FIntPoint Footprint(1, 1);

		if (Library)
		{
			if (const FMazeObjectType* Type = Library->FindType(Placement.TypeId))
			{
				Footprint = Type->FootprintCells;
			}
		}

		Add(Placement.CellXZ, Footprint);
	}
}

bool FMazeOccupancy::IsFree(const FIntPoint& MinXZ, const FIntPoint& ExtentXZ) const
{
	const FIntPoint Size = SafeExtent(ExtentXZ);

	for (int32 X = MinXZ.X; X < MinXZ.X + Size.X; ++X)
	{
		for (int32 Z = MinXZ.Y; Z < MinXZ.Y + Size.Y; ++Z)
		{
			if (Cells.Contains(FIntPoint(X, Z)))
			{
				return false;
			}
		}
	}

	return true;
}

bool FMazeOccupancy::IsClearOf(const FIntPoint& MinXZ, const FIntPoint& ExtentXZ,
                               const int32 SpacingCells) const
{
	if (SpacingCells <= 0)
	{
		return IsFree(MinXZ, ExtentXZ);
	}

	// The footprint grown by the spacing in every direction. Asking about the grown rectangle
	// is the same question as "is anything within SpacingCells of the real one", and it is one
	// loop instead of a distance test per occupied cell.
	const FIntPoint Size = SafeExtent(ExtentXZ);

	return IsFree(FIntPoint(MinXZ.X - SpacingCells, MinXZ.Y - SpacingCells),
	              FIntPoint(Size.X + SpacingCells * 2, Size.Y + SpacingCells * 2));
}
