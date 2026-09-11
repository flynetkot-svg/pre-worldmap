#include "Export/MazeCollisionBuilder.h"

#include "Data/MazeGrid.h"
#include "Data/MazeRoomDesc.h"
#include "PhysicsEngine/BoxElem.h"

namespace
{
	/** A rectangle of solid cells in the XZ plane on a single depth slice. */
	struct FSolidSlab
	{
		int32 MinX = 0;
		int32 MaxX = 0;   // inclusive
		int32 MinZ = 0;
		int32 MaxZ = 0;   // inclusive
		int32 MinY = 0;
		int32 MaxY = 0;   // inclusive

		bool MatchesXZ(const FSolidSlab& Other) const
		{
			return MinX == Other.MinX && MaxX == Other.MaxX
				&& MinZ == Other.MinZ && MaxZ == Other.MaxZ;
		}
	};
}

int32 FMazeCollisionBuilder::Build(const FMazeGrid& Grid, const FMazeRoomDesc& Room,
                                   const FVector& Origin, TArray<FKBoxElem>& OutBoxes)
{
	int32 CollisionMinY = 0;
	int32 CollisionMaxY = 0;
	Grid.GetCollisionCellRangeY(CollisionMinY, CollisionMaxY);

	TArray<FSolidSlab> Slabs;

	for (int32 Y = CollisionMinY; Y < CollisionMaxY; ++Y)
	{
		TArray<FSolidSlab> RowsForThisY;

		for (int32 Z = Room.MinXZ.Y; Z < Room.MaxXZ.Y; ++Z)
		{
			// Pass 1: contiguous runs of solid cells along X.
			int32 RunStart = INDEX_NONE;

			for (int32 X = Room.MinXZ.X; X <= Room.MaxXZ.X; ++X)
			{
				// Deliberately IsSolidForGeometry: the world borders must get collision,
				// otherwise the player walks through the edge of the map. The backdrop lies
				// outside the play band, so it will not add any extra boxes.
				//
				// The painted back wall is excluded explicitly. It normally sits far outside the
				// collision range anyway, but with a zero Background band its slice can fall into
				// the range — and a wall the player could bump into behind the maze would be a
				// bug that only shows up as an invisible obstacle in game.
				const FIntVector Cell(X, Y, Z);
				const bool bSolid = (X < Room.MaxXZ.X)
					&& Grid.IsSolidForGeometry(Cell)
					&& !Grid.IsBackWall(Cell);

				if (bSolid && RunStart == INDEX_NONE)
				{
					RunStart = X;
				}
				else if (!bSolid && RunStart != INDEX_NONE)
				{
					FSolidSlab Run;
					Run.MinX = RunStart;
					Run.MaxX = X - 1;
					Run.MinZ = Z;
					Run.MaxZ = Z;
					Run.MinY = Y;
					Run.MaxY = Y;

					// Pass 2: the same run one row down along Z — we grow the rectangle.
					// We search the whole list instead of looking at the last entry: a single
					// row can hold several runs, and Last() would miss.
					FSolidSlab* Extendable = RowsForThisY.FindByPredicate(
						[&Run, Z](const FSolidSlab& Existing)
						{
							return Existing.MinX == Run.MinX
								&& Existing.MaxX == Run.MaxX
								&& Existing.MaxZ == Z - 1;
						});

					if (Extendable)
					{
						Extendable->MaxZ = Z;
					}
					else
					{
						RowsForThisY.Add(Run);
					}

					RunStart = INDEX_NONE;
				}
			}
		}

		// Pass 3: the same rectangle on the neighbouring depth slice — we grow along Y.
		for (const FSolidSlab& Candidate : RowsForThisY)
		{
			FSolidSlab* Extendable = Slabs.FindByPredicate([&Candidate, Y](const FSolidSlab& Existing)
			{
				return Existing.MaxY == Y - 1 && Existing.MatchesXZ(Candidate);
			});

			if (Extendable)
			{
				Extendable->MaxY = Y;
			}
			else
			{
				Slabs.Add(Candidate);
			}
		}
	}

	for (const FSolidSlab& Slab : Slabs)
	{
		const FVector Min = Grid.GetCellBounds(FIntVector(Slab.MinX, Slab.MinY, Slab.MinZ)).Min;
		const FVector Max = Grid.GetCellBounds(FIntVector(Slab.MaxX, Slab.MaxY, Slab.MaxZ)).Max;

		FKBoxElem Box;
		Box.Center = (Min + Max) * 0.5 - Origin;
		Box.Rotation = FRotator::ZeroRotator;
		// X/Y/Z on FKBoxElem are the full sizes, not the half extents.
		Box.X = static_cast<float>(Max.X - Min.X);
		Box.Y = static_cast<float>(Max.Y - Min.Y);
		Box.Z = static_cast<float>(Max.Z - Min.Z);

		OutBoxes.Add(Box);
	}

	return OutBoxes.Num();
}
