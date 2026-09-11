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
