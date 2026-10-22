#include "Analysis/MazeReachability.h"

#include "Data/MazeGrid.h"

namespace
{
	/** The drawing plane as a dense solid map: TMap lookups per move would be the whole cost. */
	void BuildSolid(const FMazeGrid& Grid, TBitArray<>& OutSolid)
	{
		const int32 W = Grid.SizeXZ.X;
		const int32 H = Grid.SizeXZ.Y;
		OutSolid.Init(false, W * H);

		const int32 PlaneY = Grid.Depth.PlaneCellY();
		for (const TPair<FIntVector, FMazeCell>& Pair : Grid.Cells)
		{
			const FIntVector& C = Pair.Key;
			if (C.Y == PlaneY && Pair.Value.IsSolid() && C.X >= 0 && C.Z >= 0 && C.X < W && C.Z < H)
			{
				OutSolid[C.Z * W + C.X] = true;
			}
		}

		for (int32 Z = 0; Z < H; ++Z)
		{
			for (int32 X = 0; X < W; ++X)
			{
				if (Grid.IsBorderCell(FIntVector(X, PlaneY, Z)))
				{
					OutSolid[Z * W + X] = true;
				}
			}
		}
	}

	struct FWorld
	{
		int32 W = 0;
		int32 H = 0;
		int32 Agent = 2;
		TBitArray<> Solid;
		TArray<int32> FreeAbove; // free cells from here upwards, this one included
		TBitArray<> Ladder;

		bool Inside(int32 X, int32 Z) const { return X >= 0 && Z >= 0 && X < W && Z < H; }
		bool IsSolid(int32 X, int32 Z) const { return !Inside(X, Z) || Solid[Z * W + X]; }
		bool Fits(int32 X, int32 Z) const { return Inside(X, Z) && FreeAbove[Z * W + X] >= Agent; }

		bool OnLadder(int32 X, int32 Z) const
		{
			for (int32 K = 0; K < Agent; ++K)
			{
				if (Inside(X, Z + K) && Ladder[(Z + K) * W + X])
				{
					return true;
				}
			}
			return false;
		}

		bool Supported(int32 X, int32 Z) const { return IsSolid(X, Z - 1) || OnLadder(X, Z); }
	};

	void BuildWorld(const FMazeGrid& Grid, const FMazeReachParams& Params, FWorld& Out)
	{
		Out.W = FMath::Max(0, Grid.SizeXZ.X);
		Out.H = FMath::Max(0, Grid.SizeXZ.Y);
		Out.Agent = FMath::Max(1, Params.AgentHeightCells);
		BuildSolid(Grid, Out.Solid);

		Out.FreeAbove.SetNumZeroed(Out.W * Out.H);
		for (int32 X = 0; X < Out.W; ++X)
		{
			int32 Run = 0;
			for (int32 Z = Out.H - 1; Z >= 0; --Z)
			{
				Run = Out.Solid[Z * Out.W + X] ? 0 : Run + 1;
				Out.FreeAbove[Z * Out.W + X] = Run;
			}
		}

		Out.Ladder.Init(false, Out.W * Out.H);
		for (const FIntRect& Rect : Params.Climbables)
		{
			for (int32 Z = FMath::Max(0, Rect.Min.Y); Z < FMath::Min(Out.H, Rect.Max.Y); ++Z)
			{
				for (int32 X = FMath::Max(0, Rect.Min.X); X < FMath::Min(Out.W, Rect.Max.X); ++X)
				{
					Out.Ladder[Z * Out.W + X] = true;
				}
			}
		}
	}

	/** Clears the mass of one cell column of the plane through the whole depth, back wall kept. */
	int32 CarveCell(FMazeGrid& Grid, int32 X, int32 Z)
	{
		int32 Carved = 0;
		const int32 Depth = Grid.DepthCells();
		for (int32 Y = 0; Y < Depth; ++Y)
		{
			const FIntVector C(X, Y, Z);
			const FMazeCell* Cell = Grid.Find(C);
			if (Cell && Cell->IsSolid())
			{
				Carved += Grid.Clear(C) ? 1 : 0;
			}
		}
		return Carved;
	}
}

void MazeReachability::Solve(const FMazeGrid& Grid, const FMazeReachParams& Params, FMazeReachResult& Out)
{
	FWorld World;
	BuildWorld(Grid, Params, World);

	Out = FMazeReachResult();
	Out.Size = FIntPoint(World.W, World.H);
	Out.Standable.Init(false, World.W * World.H);
	Out.Reached.Init(false, World.W * World.H);

	for (int32 Z = 0; Z < World.H; ++Z)
	{
		for (int32 X = 0; X < World.W; ++X)
		{
			if (World.Fits(X, Z) && World.Supported(X, Z))
			{
				Out.Standable[Z * World.W + X] = true;
				++Out.NumStandable;
			}
		}
	}

	TArray<int32> Queue;
	auto Push = [&World, &Out, &Queue](int32 X, int32 Z)
	{
		if (World.Fits(X, Z) && !Out.Reached[Z * World.W + X])
		{
			Out.Reached[Z * World.W + X] = true;
			Queue.Add(Z * World.W + X);
		}
	};

	// Each start is lifted to the first height he fits at; from there gravity does the rest.
	for (const FIntPoint& Start : Params.Starts)
	{
		for (int32 Lift = 0; Lift <= World.Agent; ++Lift)
		{
			if (World.Fits(Start.X, Start.Y + Lift))
			{
				Out.UsedStarts.Add(FIntPoint(Start.X, Start.Y + Lift));
				Push(Start.X, Start.Y + Lift);
				break;
			}
		}
	}

	// No arrival point anywhere: the lowest, then leftmost, place to stand.
	if (Out.UsedStarts.Num() == 0)
	{
		for (int32 Z = 0; Z < World.H && Out.UsedStarts.Num() == 0; ++Z)
		{
			for (int32 X = 0; X < World.W; ++X)
			{
				if (Out.Standable[Z * World.W + X])
				{
					Out.UsedStarts.Add(FIntPoint(X, Z));
					Push(X, Z);
					break;
				}
			}
		}
	}

	const int32 Up = FMath::Max(0, Params.ClimbUpCells);

	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 X = Queue[Head] % World.W;
		const int32 Z = Queue[Head] / World.W;

		const bool bLadder = World.OnLadder(X, Z);
		const bool bGround = World.IsSolid(X, Z - 1);

		if (!bLadder && !bGround)
		{
			// Falling. Straight down: steering in the air would find more, and an editor check
			// that promises more than the player can do is worse than one that promises less.
			Push(X, Z - 1);
			continue;
		}

		for (const int32 Dir : { -1, 1 })
		{
			Push(X + Dir, Z);

			// A step or a jump: up first, as far as the head has room, then across.
			for (int32 K = 1; K <= Up && World.Fits(X, Z + K); ++K)
			{
				Push(X + Dir, Z + K);
			}
		}

		if (bLadder)
		{
			Push(X, Z + 1);
			Push(X, Z - 1);

			// Over the top: off the last rung and on to the floor beside it, one cell up.
			Push(X - 1, Z + 1);
			Push(X + 1, Z + 1);
		}
	}

	for (int32 I = 0; I < World.W * World.H; ++I)
	{
		Out.NumReachedStandable += (Out.Standable[I] && Out.Reached[I]) ? 1 : 0;
	}
}

void MazeReachability::FindIslands(const FMazeReachResult& Result, TArray<FMazeReachIsland>& OutIslands)
{
	OutIslands.Reset();

	const int32 W = Result.Size.X;
	const int32 H = Result.Size.Y;
	TBitArray<> Seen(false, W * H);
	TArray<FIntPoint> Stack;

	for (int32 Z = 0; Z < H; ++Z)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const int32 I = Z * W + X;
			if (Seen[I] || !Result.Standable[I] || Result.Reached[I])
			{
				continue;
			}

			FMazeReachIsland& Island = OutIslands.AddDefaulted_GetRef();
			Island.Min = Island.Max = FIntPoint(X, Z);
			Seen[I] = true;
			Stack.Reset();
			Stack.Add(FIntPoint(X, Z));

			while (Stack.Num() > 0)
			{
				const FIntPoint P = Stack.Pop(EAllowShrinking::No);
				Island.Cells.Add(P);
				Island.Min = FIntPoint(FMath::Min(Island.Min.X, P.X), FMath::Min(Island.Min.Y, P.Y));
				Island.Max = FIntPoint(FMath::Max(Island.Max.X, P.X), FMath::Max(Island.Max.Y, P.Y));

				for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					const FIntPoint N = P + D;
					if (!Result.IsInside(N.X, N.Y))
					{
						continue;
					}
					const int32 NI = N.Y * W + N.X;
					if (!Seen[NI] && Result.Standable[NI] && !Result.Reached[NI])
					{
						Seen[NI] = true;
						Stack.Add(N);
					}
				}
			}
		}
	}

	OutIslands.Sort([](const FMazeReachIsland& A, const FMazeReachIsland& B)
	{
		return A.Cells.Num() > B.Cells.Num();
	});
}

bool MazeReachability::ConnectIsland(FMazeGrid& Grid, const FMazeReachResult& Result,
                                      const FMazeReachIsland& Island, int32 AgentHeightCells,
                                      int32 ShaftWidth, bool bAllowUp, FConnection& Out)
{
	Out = FConnection();
	const int32 W = Result.Size.X;
	const int32 H = Result.Size.Y;
	if (Island.Cells.Num() == 0 || W <= 0 || H <= 0)
	{
		return false;
	}

	// The nearest pair: a breadth-first search from every reached place to stand, through
	// anything, until it touches the island. Each cell remembers which reached place it came from.
	TBitArray<> InIsland(false, W * H);
	for (const FIntPoint& P : Island.Cells)
	{
		InIsland[P.Y * W + P.X] = true;
	}

	TArray<int32> Source;
	Source.Init(INDEX_NONE, W * H);
	TArray<int32> Queue;
	Queue.Reserve(W * H / 4);
	for (int32 I = 0; I < W * H; ++I)
	{
		if (Result.Standable[I] && Result.Reached[I])
		{
			Source[I] = I;
			Queue.Add(I);
		}
	}
	if (Queue.Num() == 0)
	{
		return false;
	}

	int32 Hit = INDEX_NONE;
	for (int32 Head = 0; Head < Queue.Num() && Hit == INDEX_NONE; ++Head)
	{
		const int32 I = Queue[Head];
		const int32 X = I % W;
		const int32 Z = I / W;
		for (const FIntPoint& D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
		{
			const int32 NX = X + D.X;
			const int32 NZ = Z + D.Y;
			if (NX < 0 || NZ < 0 || NX >= W || NZ >= H)
			{
				continue;
			}
			const int32 NI = NZ * W + NX;
			if (Source[NI] != INDEX_NONE)
			{
				continue;
			}
			Source[NI] = Source[I];
			if (InIsland[NI])
			{
				Hit = NI;
				break;
			}
			Queue.Add(NI);
		}
	}
	if (Hit == INDEX_NONE)
	{
		return false;
	}

	const FIntPoint U(Hit % W, Hit / W);
	const FIntPoint R(Source[Hit] % W, Source[Hit] / W);
	const bool bUp = U.Y > R.Y;
	if (bUp && !bAllowUp)
	{
		return false;
	}

	const int32 Head = FMath::Max(2, AgentHeightCells + 1);
	const int32 Shaft = FMath::Max(2, ShaftWidth);

	// Never into the outermost ring or a closed border: the world's shell stays shut.
	const int32 PlaneY = Grid.Depth.PlaneCellY();
	auto Carve = [&](int32 X0, int32 X1, int32 Z0, int32 Z1)
	{
		for (int32 Z = FMath::Max(1, FMath::Min(Z0, Z1)); Z <= FMath::Min(H - 2, FMath::Max(Z0, Z1)); ++Z)
		{
			for (int32 X = FMath::Max(1, FMath::Min(X0, X1)); X <= FMath::Min(W - 2, FMath::Max(X0, X1)); ++X)
			{
				if (!Grid.IsBorderCell(FIntVector(X, PlaneY, Z)))
				{
					Out.CarvedCells += CarveCell(Grid, X, Z);
				}
			}
		}
	};

	// Down: the shaft opens straight over the island and he drops on to its floor.
	// Up: it rises beside the island, so the island keeps its floor to step off on to.
	int32 ShaftX = U.X;
	if (bUp)
	{
		ShaftX = (U.X - Shaft >= 1) ? U.X - Shaft : U.X + 1;
	}
	ShaftX = FMath::Clamp(ShaftX, 1, FMath::Max(1, W - 1 - Shaft));

	// Along the floor he is on, to the foot of the shaft.
	Carve(R.X, ShaftX + Shaft - 1, R.Y, R.Y + Head - 1);

	if (bUp)
	{
		Carve(ShaftX, ShaftX + Shaft - 1, R.Y, U.Y + Head - 1);
		Out.bNeedsLadder = true;
		// In the column next to the island, so the step off the top lands on its floor.
		const int32 LadderX = (ShaftX < U.X) ? ShaftX + Shaft - 1 : ShaftX;
		Out.LadderBase = FIntPoint(LadderX, R.Y);
		Out.LadderSpan = U.Y - R.Y;
	}
	else
	{
		Carve(ShaftX, ShaftX + Shaft - 1, U.Y, R.Y + Head - 1);
	}

	return Out.CarvedCells > 0 || Out.bNeedsLadder;
}
