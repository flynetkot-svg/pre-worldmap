#include "Generators/MazeLayout2D.h"

//  Passage ends, found in the drawing and given a shape.
//
//  An end is a column of air [Z0, Z1) bounded by mass above and below, with the column next
//  to it solid over the whole of that height — a wall with no door in it — and the same run
//  repeated for a stretch back the other way: a straight tube. A hole in the floor or the
//  ceiling changes the run and ends the tube, so a drop or a shaft is never closed over.

namespace
{
	/** The open part of a passage column, as fractions of its height: 0 — floor, 1 — ceiling. */
	struct FOpening
	{
		double Bottom = 0.0;
		double Top = 1.0;
	};

	/** T runs from 0 at the end wall to 1 where the shape gives back the full passage. */
	FOpening Profile(EMazeDeadEndShape Shape, double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		switch (Shape)
		{
		case EMazeDeadEndShape::Arrow:     return { 0.5 - T * 0.5, 0.5 + T * 0.5 };
		case EMazeDeadEndShape::SlantDown: return { 0.0, T };
		case EMazeDeadEndShape::SlantUp:   return { 1.0 - T, 1.0 };
		case EMazeDeadEndShape::Round:
		{
			const double Half = 0.5 * FMath::Sqrt(FMath::Max(0.0, 1.0 - (1.0 - T) * (1.0 - T)));
			return { 0.5 - Half, 0.5 + Half };
		}
		case EMazeDeadEndShape::Polygon:   return { 0.25 - T * 0.25, 0.75 + T * 0.25 };
		default:                           return { 0.0, 1.0 };
		}
	}

	/** About 45 degrees for the shape, in columns, for a passage this tall. */
	int32 AutoLength(EMazeDeadEndShape Shape, int32 Height)
	{
		switch (Shape)
		{
		case EMazeDeadEndShape::SlantDown:
		case EMazeDeadEndShape::SlantUp:   return Height;
		case EMazeDeadEndShape::Arrow:
		case EMazeDeadEndShape::Round:     return FMath::DivideAndRoundUp(Height, 2);
		case EMazeDeadEndShape::Polygon:   return FMath::DivideAndRoundUp(Height, 4);
		default:                           return 0;
		}
	}

	EMazeDeadEndShape PickShape(const TArray<FMazeDeadEndShapeChoice>& Shapes, double Total, FRandomStream& Rng)
	{
		double Roll = Rng.FRand() * Total;
		for (const FMazeDeadEndShapeChoice& Choice : Shapes)
		{
			if (Choice.Weight <= 0.0f)
			{
				continue;
			}
			Roll -= Choice.Weight;
			if (Roll <= 0.0)
			{
				return Choice.Shape;
			}
		}
		return Shapes.Last().Shape;
	}
}

int32 MazeShapeDeadEnds(FMazeLayout2D& Layout, const FMazeDeadEndParams& Params, FRandomStream& Rng)
{
	if (Params.Chance <= 0.0f || !Params.Shapes)
	{
		return 0;
	}

	double TotalWeight = 0.0;
	for (const FMazeDeadEndShapeChoice& Choice : *Params.Shapes)
	{
		TotalWeight += FMath::Max(0.0f, Choice.Weight);
	}
	if (TotalWeight <= 0.0)
	{
		return 0;
	}

	const int32 MinX = FMath::Max(1, Params.Min.X);
	const int32 MaxX = FMath::Min(Layout.Width() - 1, Params.Max.X);
	const int32 MinZ = FMath::Max(1, Params.Min.Y);
	const int32 MaxZ = FMath::Min(Layout.Height() - 1, Params.Max.Y);
	const int32 MinHeight = FMath::Max(2, Params.MinHeight);
	const int32 MaxHeight = FMath::Max(MinHeight, Params.MaxHeight);

	// Cells of ends already shaped. A tube closed at both ends is found from both sides, and
	// the second shape must stop where the first one began.
	TBitArray<> Used(false, Layout.Width() * Layout.Height());

	auto IsTaken = [&](int32 X, int32 Z)
	{
		const int32 I = Layout.Index(X, Z);
		return Used[I] || (Params.Protected && (*Params.Protected)[I]);
	};

	// The same run of air, bounded the same way, in column X.
	auto SameRun = [&](int32 X, int32 Z0, int32 Z1)
	{
		if (X < MinX || X >= MaxX || Layout.IsFree(X, Z0 - 1) || Layout.IsFree(X, Z1))
		{
			return false;
		}
		for (int32 Z = Z0; Z < Z1; ++Z)
		{
			if (!Layout.IsFree(X, Z) || IsTaken(X, Z))
			{
				return false;
			}
		}
		return true;
	};

	auto ColumnClosed = [&](int32 X, int32 Z0, int32 Z1)
	{
		for (int32 Z = Z0; Z < Z1; ++Z)
		{
			if (Layout.IsFree(X, Z))
			{
				return false;
			}
		}
		return true;
	};

	int32 Shaped = 0;

	for (int32 X = MinX; X < MaxX; ++X)
	{
		int32 Z = MinZ;
		while (Z < MaxZ)
		{
			if (!Layout.IsFree(X, Z))
			{
				++Z;
				continue;
			}

			const int32 Z0 = Z;
			while (Z < MaxZ && Layout.IsFree(X, Z))
			{
				++Z;
			}
			const int32 Z1 = Z;
			const int32 Height = Z1 - Z0;

			if (Height < MinHeight || Height > MaxHeight || Layout.IsFree(X, Z1))
			{
				continue; // too low, a hall, or open above: a shaft, not a passage
			}

			for (const int32 Dir : { 1, -1 })
			{
				// Dir 1: the wall is on the right and the tube runs back to the left.
				if (!ColumnClosed(X + Dir, Z0, Z1))
				{
					continue;
				}

				constexpr int32 MaxTube = 64;
				int32 Tube = 0;
				while (Tube < MaxTube && SameRun(X - Dir * Tube, Z0, Z1))
				{
					++Tube;
				}
				if (Tube < 2)
				{
					continue;
				}

				if (Rng.FRand() >= Params.Chance)
				{
					continue;
				}

				const EMazeDeadEndShape Shape = PickShape(*Params.Shapes, TotalWeight, Rng);
				const int32 Wanted = Params.Length > 0 ? Params.Length : AutoLength(Shape, Height);

				// At least one full-height column stays: an end shaped all the way back is a
				// passage that was quietly made shorter, or closed off altogether.
				const int32 Length = FMath::Min(Wanted, Tube - 1);

				for (int32 i = 0; i < Length; ++i)
				{
					const int32 Column = X - Dir * i;
					const FOpening Open = Profile(Shape, double(i) / Length);

					for (int32 Cz = Z0; Cz < Z1; ++Cz)
					{
						Used[Layout.Index(Column, Cz)] = true;

						// Judged at the centre of the cell, so the shape is symmetric in the grid.
						const double F = (Cz - Z0 + 0.5) / Height;
						if (F < Open.Bottom - KINDA_SMALL_NUMBER || F > Open.Top + KINDA_SMALL_NUMBER)
						{
							Layout.Set(Column, Cz, EMazeCellType::Solid);
						}
					}
				}

				++Shaped;
				break; // a run is one end at most
			}
		}
	}

	return Shaped;
}
