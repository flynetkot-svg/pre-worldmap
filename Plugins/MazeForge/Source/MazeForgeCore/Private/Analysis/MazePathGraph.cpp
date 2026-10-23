#include "Analysis/MazePathGraph.h"

#include "Analysis/MazeReachability.h"

FString FMazePathStats::ToString() const
{
	TArray<FString> Levels;
	for (int32 L = 0; L < DeadEndsPerLevel.Num(); ++L)
	{
		Levels.Add(FString::Printf(TEXT("L%d %d"), L + 1, DeadEndsPerLevel[L]));
	}

	return FString::Printf(
		TEXT("main path %d cells to the %s at (%d, %d); %d branch(es) from %d junction(s), depth up to %d "
		     "(avg %.0f), nesting up to %d; %d dead end(s) [%s]; %d loop(s); confusion %d/100"),
		MainPathCells, bExitFromGate ? TEXT("Gate") : TEXT("farthest point"), Exit.X, Exit.Y,
		Branches, MainJunctions, MaxBranchDepth, AvgBranchDepth, MaxNesting, DeadEnds,
		*FString::Join(Levels, TEXT(", ")), Loops, Confusion);
}

bool MazePaths::Build(const FMazeReachResult& Result, const TArray<FIntPoint>& Exits,
                      int32 BandCells, FMazePathGraph& Out)
{
	Out = FMazePathGraph();
	const int32 W = Result.Size.X;
	const int32 H = Result.Size.Y;
	const int32 Count = W * H;
	const int32 B = FMath::Max(1, BandCells);
	Out.BandCells = B;

	if (Count == 0 || Result.Distance.Num() != Count || Result.UsedStarts.Num() == 0)
	{
		return false;
	}

	auto Dist = [&Result](int32 I) { return Result.Distance[I]; };

	// --------------------------------------------------------------- start and exit
	Out.Stats.Start = Result.UsedStarts[0];

	// A Gate point: the reached position nearest to it, within a few cells.
	int32 ExitIndex = INDEX_NONE;
	for (const FIntPoint& Gate : Exits)
	{
		int32 Best = INDEX_NONE;
		int32 BestD2 = MAX_int32;
		for (int32 Dz = -4; Dz <= 4; ++Dz)
		{
			for (int32 Dx = -4; Dx <= 4; ++Dx)
			{
				const int32 X = Gate.X + Dx;
				const int32 Z = Gate.Y + Dz;
				if (Result.IsReached(X, Z) && Dx * Dx + Dz * Dz < BestD2)
				{
					Best = Result.Index(X, Z);
					BestD2 = Dx * Dx + Dz * Dz;
				}
			}
		}
		if (Best != INDEX_NONE && (ExitIndex == INDEX_NONE || Dist(Best) > Dist(ExitIndex)))
		{
			ExitIndex = Best;
		}
	}
	Out.Stats.bExitFromGate = ExitIndex != INDEX_NONE;

	if (ExitIndex == INDEX_NONE)
	{
		for (int32 I = 0; I < Count; ++I)
		{
			if (Result.Standable[I] && Result.Reached[I] && (ExitIndex == INDEX_NONE || Dist(I) > Dist(ExitIndex)))
			{
				ExitIndex = I;
			}
		}
	}
	if (ExitIndex == INDEX_NONE)
	{
		return false;
	}
	Out.Stats.Exit = FIntPoint(ExitIndex % W, ExitIndex / W);

	// --------------------------------------------------------------- nodes: band pieces
	TArray<int32> NodeOf;
	NodeOf.Init(INDEX_NONE, Count);
	TArray<FVector2f> Sum;
	TArray<int32> Stack;

	static const FIntPoint Steps[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

	for (int32 I = 0; I < Count; ++I)
	{
		if (!Result.Reached[I] || NodeOf[I] != INDEX_NONE)
		{
			continue;
		}

		const int32 Band = Dist(I) / B;
		const int32 Node = Out.Nodes.AddDefaulted();
		Out.Nodes[Node].Band = Band;
		Sum.Add(FVector2f::ZeroVector);

		NodeOf[I] = Node;
		Stack.Reset();
		Stack.Add(I);
		while (Stack.Num() > 0)
		{
			const int32 P = Stack.Pop(EAllowShrinking::No);
			const int32 X = P % W;
			const int32 Z = P / W;
			Sum[Node] += FVector2f(X + 0.5f, Z);
			++Out.Nodes[Node].Cells;

			for (const FIntPoint& S : Steps)
			{
				const int32 NX = X + S.X;
				const int32 NZ = Z + S.Y;
				if (NX < 0 || NZ < 0 || NX >= W || NZ >= H)
				{
					continue;
				}
				const int32 N = NZ * W + NX;
				if (Result.Reached[N] && NodeOf[N] == INDEX_NONE && Dist(N) / B == Band)
				{
					NodeOf[N] = Node;
					Stack.Add(N);
				}
			}
		}
	}

	for (int32 N = 0; N < Out.Nodes.Num(); ++N)
	{
		Out.Nodes[N].Centre = Sum[N] / static_cast<float>(FMath::Max(1, Out.Nodes[N].Cells));
	}

	// --------------------------------------------------------------- edges: band to the next
	TSet<uint64> Seen;
	for (int32 I = 0; I < Count; ++I)
	{
		if (!Result.Reached[I])
		{
			continue;
		}
		const int32 X = I % W;
		const int32 Z = I / W;
		const int32 From = NodeOf[I];

		for (const FIntPoint& S : Steps)
		{
			const int32 NX = X + S.X;
			const int32 NZ = Z + S.Y;
			if (NX < 0 || NZ < 0 || NX >= W || NZ >= H)
			{
				continue;
			}
			const int32 J = NZ * W + NX;
			if (!Result.Reached[J])
			{
				continue;
			}
			const int32 To = NodeOf[J];
			if (Out.Nodes[To].Band != Out.Nodes[From].Band + 1)
			{
				continue;
			}
			const uint64 Key = (uint64(uint32(From)) << 32) | uint32(To);
			if (!Seen.Contains(Key))
			{
				Seen.Add(Key);
				Out.Nodes[From].Next.Add(To);
				Out.Nodes[To].Prev.Add(From);
			}
		}
	}

	const FIntPoint StartCell = Out.Stats.Start;
	Out.StartNode = Result.IsReached(StartCell.X, StartCell.Y) ? NodeOf[Result.Index(StartCell.X, StartCell.Y)] : INDEX_NONE;
	Out.ExitNode = NodeOf[ExitIndex];
	if (Out.StartNode == INDEX_NONE)
	{
		// Several starts, or the first one lifted: whichever node sits in band 0.
		Out.StartNode = Out.Nodes.IndexOfByPredicate([](const FMazePathNode& Node) { return Node.Band == 0; });
	}

	// --------------------------------------------------------------- main path
	for (int32 N = Out.ExitNode; N != INDEX_NONE;)
	{
		Out.Nodes[N].bMain = true;
		if (Out.Nodes[N].Band == 0 || Out.Nodes[N].Prev.Num() == 0)
		{
			break;
		}
		N = Out.Nodes[N].Prev[0];
	}
	Out.Stats.MainPathCells = Dist(ExitIndex);

	// --------------------------------------------------------------- branches
	// Each step off the main path starts a branch; inside it, a piece with two or more ways on is
	// a junction, and everything past one is a level deeper.
	int32 BranchCount = 0;
	int64 DepthSum = 0;
	TArray<int32> Queue;

	for (int32 M = 0; M < Out.Nodes.Num(); ++M)
	{
		const FMazePathNode& Main = Out.Nodes[M];
		if (!Main.bMain)
		{
			continue;
		}

		bool bJunction = false;
		for (const int32 First : Main.Next)
		{
			if (Out.Nodes[First].bMain || Out.Nodes[First].Branch != INDEX_NONE)
			{
				continue;
			}

			bJunction = true;
			const int32 BranchId = BranchCount++;
			int32 MaxBand = Out.Nodes[First].Band;

			Out.Nodes[First].Branch = BranchId;
			Out.Nodes[First].Level = 1;
			Queue.Reset();
			Queue.Add(First);

			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				FMazePathNode& Node = Out.Nodes[Queue[Head]];
				MaxBand = FMath::Max(MaxBand, Node.Band);

				int32 Onward = 0;
				for (const int32 Next : Node.Next)
				{
					Onward += Out.Nodes[Next].bMain ? 0 : 1;
				}

				if (Onward == 0)
				{
					++Out.Stats.DeadEnds;
					const int32 Level = FMath::Max(1, Node.Level);
					if (Out.Stats.DeadEndsPerLevel.Num() < Level)
					{
						Out.Stats.DeadEndsPerLevel.SetNumZeroed(Level);
					}
					++Out.Stats.DeadEndsPerLevel[Level - 1];
					Out.Stats.MaxNesting = FMath::Max(Out.Stats.MaxNesting, Level);
					continue;
				}

				// The first way on carries the branch; every other one is a branch off it.
				int32 Taken = 0;
				for (const int32 Next : Node.Next)
				{
					FMazePathNode& Child = Out.Nodes[Next];
					if (Child.bMain || Child.Branch != INDEX_NONE)
					{
						continue;
					}
					Child.Branch = BranchId;
					Child.Level = Node.Level + (Taken > 0 ? 1 : 0);
					++Taken;
					Queue.Add(Next);
				}
			}

			const int32 Depth = (MaxBand - Main.Band) * B;
			Out.Stats.MaxBranchDepth = FMath::Max(Out.Stats.MaxBranchDepth, Depth);
			DepthSum += Depth;
		}

		Out.Stats.MainJunctions += bJunction ? 1 : 0;
	}

	Out.Stats.Branches = BranchCount;
	Out.Stats.AvgBranchDepth = BranchCount > 0 ? static_cast<float>(DepthSum) / BranchCount : 0.0f;

	for (const FMazePathNode& Node : Out.Nodes)
	{
		Out.Stats.Loops += FMath::Max(0, Node.Prev.Num() - 1);
	}

	// --------------------------------------------------------------- confusion, 0..100
	// Measured in bands, so the grid's resolution does not change the answer: one branch every
	// eight bands of main path, branches a quarter of the main path deep, three levels of nesting
	// and a loop for every three branches each score full marks.
	const float MainBands = FMath::Max(1.0f, static_cast<float>(Out.Stats.MainPathCells) / B);
	const float Frequency = FMath::Min(1.0f, BranchCount / FMath::Max(1.0f, MainBands / 8.0f));
	const float Depth = FMath::Min(1.0f, (Out.Stats.AvgBranchDepth / B) / FMath::Max(1.0f, MainBands * 0.25f));
	const float Nesting = FMath::Min(1.0f, Out.Stats.MaxNesting / 3.0f);
	const float LoopShare = FMath::Min(1.0f, Out.Stats.Loops / FMath::Max(1.0f, BranchCount / 3.0f));
	Out.Stats.Confusion = FMath::RoundToInt(100.0f * (0.4f * Frequency + 0.3f * Depth + 0.2f * Nesting + 0.1f * LoopShare));

	Out.NodeOf = MoveTemp(NodeOf);
	return true;
}
