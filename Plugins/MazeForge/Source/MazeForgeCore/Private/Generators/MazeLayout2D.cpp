#include "Generators/MazeLayout2D.h"

#include "Data/MazeGrid.h"

namespace
{
	const FIntPoint GNeighbours[4] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)
	};
}

FMazeLayout2D::FMazeLayout2D(const FIntPoint& InSize)
	: Size(FIntPoint(FMath::Max(1, InSize.X), FMath::Max(1, InSize.Y)))
{
	Cells.SetNumZeroed(Size.X * Size.Y);
}

bool FMazeLayout2D::CanStand(int32 X, int32 Z, int32 AgentHeight) const
{
	const int32 Height = FMath::Max(1, AgentHeight);

	for (int32 Offset = 0; Offset < Height; ++Offset)
	{
		if (!IsFree(X, Z + Offset))
		{
			return false;
		}
	}

	return true;
}

void FMazeLayout2D::Fill(EMazeCellType Type)
{
	for (EMazeCellType& Cell : Cells)
	{
		Cell = Type;
	}
}

void FMazeLayout2D::FillRect(const FIntPoint& Min, const FIntPoint& Max, EMazeCellType Type)
{
	const int32 MinX = FMath::Max(0, Min.X);
	const int32 MinZ = FMath::Max(0, Min.Y);
	const int32 MaxX = FMath::Min(Size.X, Max.X);
	const int32 MaxZ = FMath::Min(Size.Y, Max.Y);

	for (int32 Z = MinZ; Z < MaxZ; ++Z)
	{
		for (int32 X = MinX; X < MaxX; ++X)
		{
			Cells[Index(X, Z)] = Type;
		}
	}
}

void FMazeLayout2D::FillRectIfSolid(const FIntPoint& Min, const FIntPoint& Max, EMazeCellType Type)
{
	const int32 MinX = FMath::Max(0, Min.X);
	const int32 MinZ = FMath::Max(0, Min.Y);
	const int32 MaxX = FMath::Min(Size.X, Max.X);
	const int32 MaxZ = FMath::Min(Size.Y, Max.Y);

	for (int32 Z = MinZ; Z < MaxZ; ++Z)
	{
		for (int32 X = MinX; X < MaxX; ++X)
		{
			const int32 At = Index(X, Z);
			if (Cells[At] == EMazeCellType::Solid)
			{
				Cells[At] = Type;
			}
		}
	}
}

int32 FMazeLayout2D::FloodFill(const FIntPoint& Start, int32 AgentHeight,
                               TBitArray<>& OutStandable) const
{
	OutStandable.Init(false, Cells.Num());

	if (!IsInside(Start.X, Start.Y) || !CanStand(Start.X, Start.Y, AgentHeight))
	{
		return 0;
	}

	// An explicit stack instead of recursion: a maze can be hundreds of thousands of
	// cells, and at that size the recursive variant overflows the thread stack.
	TArray<FIntPoint> Stack;
	Stack.Reserve(256);
	Stack.Add(Start);
	OutStandable[Index(Start.X, Start.Y)] = true;

	int32 Count = 0;

	while (Stack.Num() > 0)
	{
		const FIntPoint At = Stack.Pop(EAllowShrinking::No);
		++Count;

		for (const FIntPoint& Step : GNeighbours)
		{
			const int32 NX = At.X + Step.X;
			const int32 NZ = At.Y + Step.Y;

			// A neighbouring position only counts if the agent fits into it whole.
			if (!IsInside(NX, NZ) || !CanStand(NX, NZ, AgentHeight))
			{
				continue;
			}

			const int32 NeighbourIndex = Index(NX, NZ);
			if (!OutStandable[NeighbourIndex])
			{
				OutStandable[NeighbourIndex] = true;
				Stack.Add(FIntPoint(NX, NZ));
			}
		}
	}

	return Count;
}

void FMazeLayout2D::BuildOccupied(const TBitArray<>& Standable, int32 AgentHeight,
                                  TBitArray<>& OutOccupied) const
{
	OutOccupied.Init(false, Cells.Num());

	const int32 Height = FMath::Max(1, AgentHeight);

	for (int32 Z = 0; Z < Size.Y; ++Z)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			if (!Standable[Index(X, Z)])
			{
				continue;
			}

			for (int32 Offset = 0; Offset < Height; ++Offset)
			{
				if (IsInside(X, Z + Offset))
				{
					OutOccupied[Index(X, Z + Offset)] = true;
				}
			}
		}
	}
}

void FMazeLayout2D::FindIslands(const TBitArray<>& Standable, int32 AgentHeight,
                                TArray<FIntPoint>& OutIslandSeeds,
                                TArray<int32>& OutIslandSizes) const
{
	OutIslandSeeds.Reset();
	OutIslandSizes.Reset();

	TBitArray<> Visited;
	Visited.Init(false, Cells.Num());

	TArray<FIntPoint> Stack;

	for (int32 Z = 0; Z < Size.Y; ++Z)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			const int32 At = Index(X, Z);
			if (Visited[At] || Standable[At] || !CanStand(X, Z, AgentHeight))
			{
				continue;
			}

			// A new cavity: we walk it whole so as not to return the same one twice.
			int32 IslandSize = 0;
			Stack.Reset();
			Stack.Add(FIntPoint(X, Z));
			Visited[At] = true;

			while (Stack.Num() > 0)
			{
				const FIntPoint Current = Stack.Pop(EAllowShrinking::No);
				++IslandSize;

				for (const FIntPoint& Step : GNeighbours)
				{
					const int32 NX = Current.X + Step.X;
					const int32 NZ = Current.Y + Step.Y;

					if (!IsInside(NX, NZ) || !CanStand(NX, NZ, AgentHeight))
					{
						continue;
					}

					const int32 NeighbourIndex = Index(NX, NZ);
					if (!Visited[NeighbourIndex])
					{
						Visited[NeighbourIndex] = true;
						Stack.Add(FIntPoint(NX, NZ));
					}
				}
			}

			OutIslandSeeds.Add(FIntPoint(X, Z));
			OutIslandSizes.Add(IslandSize);
		}
	}
}

int32 FMazeLayout2D::FloodSolidBounded(const FIntPoint& Seed, const TBitArray<>& Forbidden)
{
	if (!IsInside(Seed.X, Seed.Y) || !IsFree(Seed.X, Seed.Y))
	{
		return 0;
	}

	TArray<FIntPoint> Stack;
	Stack.Add(Seed);

	int32 Filled = 0;

	while (Stack.Num() > 0)
	{
		const FIntPoint At = Stack.Pop(EAllowShrinking::No);

		// We check on the pop from the stack rather than on the push: one cell can get
		// onto the stack from two sides.
		if (!IsFree(At.X, At.Y) || Forbidden[Index(At.X, At.Y)])
		{
			continue;
		}

		Cells[Index(At.X, At.Y)] = EMazeCellType::Solid;
		++Filled;

		for (const FIntPoint& Step : GNeighbours)
		{
			const int32 NX = At.X + Step.X;
			const int32 NZ = At.Y + Step.Y;

			if (IsInside(NX, NZ) && IsFree(NX, NZ) && !Forbidden[Index(NX, NZ)])
			{
				Stack.Add(FIntPoint(NX, NZ));
			}
		}
	}

	return Filled;
}

FIntPoint FMazeLayout2D::FindNearestReachable(const FIntPoint& From, const TBitArray<>& Standable) const
{
	FIntPoint Best(INDEX_NONE, INDEX_NONE);
	int32 BestDistanceSq = MAX_int32;

	for (int32 Z = 0; Z < Size.Y; ++Z)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			if (!Standable[Index(X, Z)])
			{
				continue;
			}

			const int32 DX = X - From.X;
			const int32 DZ = Z - From.Y;
			const int32 DistanceSq = DX * DX + DZ * DZ;

			if (DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				Best = FIntPoint(X, Z);
			}
		}
	}

	return Best;
}

void MazeWriteLayoutToGrid(const FMazeLayout2D& Layout, FMazeGrid& InOutGrid)
{
	// We write FLAT: one cell per column, in the drawing plane.
	//
	// This used to be SetColumnXZ, and the result was immediately stretched over the whole
	// depth. On a 707x376 maze that gave close to two hundred thousand cells, and the
	// preview is rebuilt on every brush movement — editing became impossible.
	//
	// The volume is grown by the separate Build Depth Volume button, once the layout is
	// already satisfactory. Draw flat, grow once — cheaper and easier to follow.
	const int32 PlaneY = InOutGrid.Depth.PlaneCellY();

	const int32 Width = FMath::Min(Layout.Width(), InOutGrid.SizeXZ.X);
	const int32 Height = FMath::Min(Layout.Height(), InOutGrid.SizeXZ.Y);

	for (int32 Z = 0; Z < Height; ++Z)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const EMazeCellType Type = Layout.Get(X, Z);

			if (Type != EMazeCellType::Empty)
			{
				InOutGrid.Set(FIntVector(X, PlaneY, Z), FMazeCell(Type));
			}
		}
	}
}

// --------------------------------------------------------------------- erosion

int32 MazeErodeOutline(FMazeLayout2D& Layout, const FMazeErosionParams& Params, FRandomStream& Rng)
{
	const float Amount = FMath::Clamp(Params.Amount, 0.0f, 1.0f);
	if (Amount <= 0.0f || (!Params.bFloors && !Params.bCeilings && !Params.bWalls))
	{
		return 0;
	}

	const int32 Keep = FMath::Max(1, Params.KeepWallCells);
	const int32 Head = FMath::Max(1, Params.AgentHeightCells);
	const int32 Passes = FMath::Clamp(Params.Passes, 1, 6);

	int32 Changed = 0;

	for (int32 Pass = 0; Pass < Passes; ++Pass)
	{
		// Read the whole boundary first, then write. Eroding in place means the second cell
		// of a run is judged against a neighbour the first one just changed, and the bites
		// walk across the map in the direction of the loop — a texture nobody asked for, and
		// one that is invisible until somebody notices every notch faces left.
		TArray<FIntPoint> ToCarve;
		TArray<FIntPoint> ToFill;

		for (int32 X = Params.Min.X + 1; X < Params.Max.X - 1; ++X)
		{
			for (int32 Z = Params.Min.Y + 1; Z < Params.Max.Y - 1; ++Z)
			{
				if (Rng.FRand() > Amount)
				{
					continue;
				}

				// After the roll, so a run with nothing protected draws exactly the numbers it
				// drew before this check existed — and a dungeon keeps its shape for its seed.
				if (Params.Protected && (*Params.Protected)[Layout.Index(X, Z)])
				{
					continue;
				}

				const bool bSolid = !Layout.IsFree(X, Z);

				const bool bFreeLeft = Layout.IsFree(X - 1, Z);
				const bool bFreeRight = Layout.IsFree(X + 1, Z);
				const bool bFreeBelow = Layout.IsFree(X, Z - 1);
				const bool bFreeAbove = Layout.IsFree(X, Z + 1);

				// Only the boundary. A cell with mass on every side is not an outline, and a
				// bite taken out of the middle of the rock is a bubble nobody will ever see.
				const int32 FreeNeighbours = (bFreeLeft ? 1 : 0) + (bFreeRight ? 1 : 0)
				                           + (bFreeBelow ? 1 : 0) + (bFreeAbove ? 1 : 0);

				if (FreeNeighbours == 0 || FreeNeighbours == 4)
				{
					continue;
				}

				// Which surface this cell belongs to, read off where the air is.
				//
				// A solid cell with air above it is the top of a floor; with air below it, the
				// underside of a ceiling. An empty cell resting on mass is standing ON a floor,
				// so filling it raises that floor — the same surface, changed from the other
				// side. That symmetry is the point: protecting the mass alone would leave the
				// walkable line just as lumpy, only with the lumps added instead of bitten.
				//
				// A corner is several surfaces at once, and the strictest answer wins: a bite
				// at the corner of a ledge still chews the edge somebody runs along.
				const bool bFloorSurface = bSolid ? bFreeAbove : !bFreeBelow;
				const bool bCeilingSurface = bSolid ? bFreeBelow : !bFreeAbove;
				const bool bWallSurface = bSolid
					? (bFreeLeft || bFreeRight)
					: (!bFreeLeft || !bFreeRight);

				if (bFloorSurface && !Params.bFloors)
				{
					continue;
				}

				if (bCeilingSurface && !Params.bCeilings)
				{
					continue;
				}

				// Only when it is nothing else: a cell that is both is already past the two
				// tests above, and refusing it here would mean the wall flag could veto a
				// ceiling that its own flag allowed.
				if (bWallSurface && !bFloorSurface && !bCeilingSurface && !Params.bWalls)
				{
					continue;
				}

				if (bSolid)
				{
					// Biting mass away. Refused where the mass is thin: along X it is a wall
					// between two chambers, along Z it is the floor somebody stands on and
					// the ceiling over their head.
					bool bThin = false;

					for (int32 D = 1; D <= Keep && !bThin; ++D)
					{
						bThin = (Layout.IsFree(X - D, Z) && Layout.IsFree(X + D, Z))
						     || (Layout.IsFree(X, Z - D) && Layout.IsFree(X, Z + D));
					}

					if (bThin)
					{
						continue;
					}

					ToCarve.Add(FIntPoint(X, Z));
				}
				else
				{
					// Sticking mass back on. Only where it hangs off the side of something,
					// never where it would drop into the headroom above a floor: an outcrop
					// growing down from a ceiling is scenery, the same outcrop at knee height
					// over a walkable floor is a passage that closed itself.
					bool bBlocksHead = false;

					for (int32 D = 1; D <= Head && !bBlocksHead; ++D)
					{
						bBlocksHead = !Layout.IsFree(X, Z - D);
					}

					if (bBlocksHead)
					{
						continue;
					}

					ToFill.Add(FIntPoint(X, Z));
				}
			}
		}

		for (const FIntPoint& Cell : ToCarve)
		{
			Layout.Set(Cell.X, Cell.Y, EMazeCellType::Empty);
		}

		for (const FIntPoint& Cell : ToFill)
		{
			Layout.Set(Cell.X, Cell.Y, EMazeCellType::Solid);
		}

		Changed += ToCarve.Num() + ToFill.Num();
	}

	return Changed;
}
