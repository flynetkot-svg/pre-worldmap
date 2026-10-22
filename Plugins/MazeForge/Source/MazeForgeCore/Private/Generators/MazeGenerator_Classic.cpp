#include "Generators/MazeGenerator_Classic.h"

#include "Data/MazeGrid.h"
#include "Generators/MazeLayout2D.h"
#include "MazeForgeCore.h"

#define LOCTEXT_NAMESPACE "MazeForge"

namespace
{
	/** The lattice: corridor cells and which of their walls are open. Bit 0 — right, bit 1 — up. */
	struct FClassicLattice
	{
		int32 Cols = 0;
		int32 Rows = 0;
		TArray<uint8> Open;

		static constexpr uint8 OpenRight = 1;
		static constexpr uint8 OpenUp = 2;

		FClassicLattice(int32 InCols, int32 InRows) : Cols(InCols), Rows(InRows)
		{
			Open.SetNumZeroed(Cols * Rows);
		}

		int32 Index(int32 C, int32 R) const { return R * Cols + C; }
		bool IsInside(int32 C, int32 R) const { return C >= 0 && C < Cols && R >= 0 && R < Rows; }

		/** Opens the wall between two neighbouring cells, whichever way round they are given. */
		void Link(FIntPoint A, FIntPoint B)
		{
			if (B.X < A.X || B.Y < A.Y)
			{
				Swap(A, B);
			}
			Open[Index(A.X, A.Y)] |= (B.X > A.X) ? OpenRight : OpenUp;
		}

		bool IsLinked(FIntPoint A, FIntPoint B) const
		{
			if (B.X < A.X || B.Y < A.Y)
			{
				Swap(A, B);
			}
			return (Open[Index(A.X, A.Y)] & ((B.X > A.X) ? OpenRight : OpenUp)) != 0;
		}
	};

	const FIntPoint Directions[4] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

	float Weight(const FIntPoint& Dir, float VerticalBias)
	{
		return Dir.Y != 0 ? VerticalBias : 1.0f;
	}

	/** One of the candidates, picked with probability proportional to its weight. */
	template <typename T, typename FWeightOf>
	int32 PickWeighted(const TArray<T>& Candidates, FWeightOf WeightOf, FRandomStream& Rng)
	{
		float Total = 0.0f;
		for (const T& Candidate : Candidates)
		{
			Total += WeightOf(Candidate);
		}

		float Roll = Rng.FRand() * Total;
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			Roll -= WeightOf(Candidates[i]);
			if (Roll <= 0.0f)
			{
				return i;
			}
		}
		return Candidates.Num() - 1;
	}

	/** Depth-first with a stack: walks until it is stuck, then backs up. Long corridors. */
	void CarveBacktracker(FClassicLattice& L, float VerticalBias, FRandomStream& Rng)
	{
		TBitArray<> Visited(false, L.Cols * L.Rows);
		TArray<FIntPoint> Stack;
		Stack.Push(FIntPoint(0, 0));
		Visited[0] = true;

		TArray<FIntPoint> Next;
		while (Stack.Num() > 0)
		{
			const FIntPoint Cell = Stack.Last();

			Next.Reset();
			for (const FIntPoint& Dir : Directions)
			{
				const FIntPoint N = Cell + Dir;
				if (L.IsInside(N.X, N.Y) && !Visited[L.Index(N.X, N.Y)])
				{
					Next.Add(Dir);
				}
			}

			if (Next.Num() == 0)
			{
				Stack.Pop();
				continue;
			}

			const FIntPoint Dir = Next[PickWeighted(Next,
				[VerticalBias](const FIntPoint& D) { return Weight(D, VerticalBias); }, Rng)];
			const FIntPoint N = Cell + Dir;
			L.Link(Cell, N);
			Visited[L.Index(N.X, N.Y)] = true;
			Stack.Push(N);
		}
	}

	/** Randomised Prim: grows from every edge of the visited region at once. Many short branches. */
	void CarvePrim(FClassicLattice& L, float VerticalBias, FRandomStream& Rng)
	{
		struct FEdge { FIntPoint From; FIntPoint Dir; };

		TBitArray<> Visited(false, L.Cols * L.Rows);
		TArray<FEdge> Frontier;

		auto Visit = [&](const FIntPoint& Cell)
		{
			Visited[L.Index(Cell.X, Cell.Y)] = true;
			for (const FIntPoint& Dir : Directions)
			{
				const FIntPoint N = Cell + Dir;
				if (L.IsInside(N.X, N.Y) && !Visited[L.Index(N.X, N.Y)])
				{
					Frontier.Add({ Cell, Dir });
				}
			}
		};

		Visit(FIntPoint(0, 0));
		while (Frontier.Num() > 0)
		{
			const int32 Picked = PickWeighted(Frontier,
				[VerticalBias](const FEdge& E) { return Weight(E.Dir, VerticalBias); }, Rng);
			const FEdge Edge = Frontier[Picked];
			Frontier.RemoveAtSwap(Picked);

			const FIntPoint N = Edge.From + Edge.Dir;
			if (Visited[L.Index(N.X, N.Y)])
			{
				continue;
			}

			L.Link(Edge.From, N);
			Visit(N);
		}
	}

	/** Knocks through a share of the walls still standing. Returns how many. */
	int32 AddLoops(FClassicLattice& L, float LoopChance, float VerticalBias, FRandomStream& Rng)
	{
		int32 Added = 0;
		for (int32 R = 0; R < L.Rows; ++R)
		{
			for (int32 C = 0; C < L.Cols; ++C)
			{
				for (const FIntPoint& Dir : { FIntPoint(1, 0), FIntPoint(0, 1) })
				{
					const FIntPoint A(C, R);
					const FIntPoint B = A + Dir;
					if (!L.IsInside(B.X, B.Y) || L.IsLinked(A, B))
					{
						continue;
					}
					if (Rng.FRand() < LoopChance * Weight(Dir, VerticalBias))
					{
						L.Link(A, B);
						++Added;
					}
				}
			}
		}
		return Added;
	}

	/** The cell farthest from the start by walking, and how far. Where the finish wants to go. */
	FIntPoint FindFarthest(const FClassicLattice& L, int32& OutSteps)
	{
		TArray<int32> Distance;
		Distance.Init(INDEX_NONE, L.Cols * L.Rows);
		TArray<FIntPoint> Queue;
		Queue.Add(FIntPoint(0, 0));
		Distance[0] = 0;

		FIntPoint Farthest(0, 0);
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const FIntPoint Cell = Queue[Head];
			const int32 D = Distance[L.Index(Cell.X, Cell.Y)];
			if (D > Distance[L.Index(Farthest.X, Farthest.Y)])
			{
				Farthest = Cell;
			}

			for (const FIntPoint& Dir : Directions)
			{
				const FIntPoint N = Cell + Dir;
				if (L.IsInside(N.X, N.Y) && Distance[L.Index(N.X, N.Y)] == INDEX_NONE
					&& L.IsLinked(Cell, N))
				{
					Distance[L.Index(N.X, N.Y)] = D + 1;
					Queue.Add(N);
				}
			}
		}

		OutSteps = Distance[L.Index(Farthest.X, Farthest.Y)];
		return Farthest;
	}
}

UMazeGenerator_Classic::UMazeGenerator_Classic()
{
	// A maze of tubes reads as one when it is a little worn and its ends are not all square.
	ErosionAmount = 0.3f;
	DeadEndChance = 0.5f;
}

FText UMazeGenerator_Classic::GetDisplayName() const
{
	return LOCTEXT("GeneratorClassic", "Classic");
}

void UMazeGenerator_Classic::Generate(FMazeGrid& InOutGrid, FRandomStream& Rng)
{
	const FIntPoint Size = InOutGrid.SizeXZ;

	// The working area is inset by the closed world borders, as in the other generators: the
	// grid draws those itself, and a corridor cut into one would let the player out.
	const int32 Border = InOutGrid.Borders.ThicknessCells;
	const FIntPoint AreaMin(InOutGrid.Borders.bCloseLeft ? Border : 0,
	                        InOutGrid.Borders.bCloseBottom ? Border : 0);
	const FIntPoint AreaMax(Size.X - (InOutGrid.Borders.bCloseRight ? Border : 0),
	                        Size.Y - (InOutGrid.Borders.bCloseTop ? Border : 0));

	const int32 PitchX = CorridorWidth + WallThickness;
	const int32 PitchZ = CorridorHeight + FloorThickness;
	const int32 Cols = (AreaMax.X - AreaMin.X - WallThickness) / PitchX;
	const int32 Rows = (AreaMax.Y - AreaMin.Y - FloorThickness) / PitchZ;

	if (Cols < 1 || Rows < 1 || Cols * Rows < 2)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Classic: a %dx%d working area holds no maze of %dx%d cells with walls of %d and "
			     "floors of %d. Make the grid larger or the cells smaller."),
			AreaMax.X - AreaMin.X, AreaMax.Y - AreaMin.Y, CorridorWidth, CorridorHeight,
			WallThickness, FloorThickness);
		return;
	}

	// Said before anything is drawn: a corridor lower than the player is a wall to him, and the
	// dead-end shaping skips such passages as crawlspaces — so this one setting quietly turns off
	// both the walking and the shapes, and nothing else would say so.
	if (CorridorHeight < AgentHeightCells)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Classic: Corridor Height %d is lower than Agent Height Cells %d — the player does "
			     "not fit, and no dead end gets a shape. Raise the corridor or lower the agent."),
			CorridorHeight, AgentHeightCells);
	}

	// The other way the shapes go missing: every corridor taller than what counts as a passage.
	if (DeadEndChance > 0.0f && CorridorHeight > DeadEndMaxHeight)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Classic: Corridor Height %d is above Dead End Max Height %d — every corridor counts "
			     "as a hall, and no dead end gets a shape. Raise Dead End Max Height to at least %d."),
			CorridorHeight, DeadEndMaxHeight, CorridorHeight);
	}

	const int32 Hole = FMath::Min(HoleWidth, CorridorWidth);
	if (CorridorWidth < 2 * Hole)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Classic: Corridor Width %d cannot keep two holes of %d apart — a ladder may stand "
			     "over the hole below it. Make it at least %d."), CorridorWidth, Hole, 2 * Hole);
	}

	// 1. The maze itself, on the lattice.
	FClassicLattice Lattice(Cols, Rows);
	if (Style == EMazeClassicStyle::Branchy)
	{
		CarvePrim(Lattice, VerticalBias, Rng);
	}
	else
	{
		CarveBacktracker(Lattice, VerticalBias, Rng);
	}
	const int32 Loops = LoopChance > 0.0f ? AddLoops(Lattice, LoopChance, VerticalBias, Rng) : 0;

	// 2. Mass. Everything solid, the slabs between rows as Floor, then the corridors carved out.
	FMazeLayout2D Layout(Size);
	Layout.Fill(EMazeCellType::Solid);

	const int32 UsedMaxX = AreaMin.X + WallThickness + Cols * PitchX;
	for (int32 R = 0; R <= Rows; ++R)
	{
		const int32 Z = AreaMin.Y + R * PitchZ;
		Layout.FillRect(FIntPoint(AreaMin.X, Z), FIntPoint(UsedMaxX, Z + FloorThickness),
			EMazeCellType::Floor);
	}

	auto CellMin = [&](int32 C, int32 R)
	{
		return FIntPoint(AreaMin.X + WallThickness + C * PitchX, AreaMin.Y + FloorThickness + R * PitchZ);
	};

	// The shaping that runs when the drawing is written (dead ends, erosion) must leave each
	// ladder alone: its column, the floor it stands on, and the headroom where the player steps
	// off at the top. An outcrop there is a ladder the export refuses to build, or one nobody
	// can get off.
	int32 Ladders = 0;
	for (int32 R = 0; R < Rows; ++R)
	{
		for (int32 C = 0; C < Cols; ++C)
		{
			const FIntPoint Min = CellMin(C, R);
			Layout.FillRect(Min, Min + FIntPoint(CorridorWidth, CorridorHeight), EMazeCellType::Empty);

			const uint8 Open = Lattice.Open[Lattice.Index(C, R)];

			// Sideways: a doorway the full height of the corridor.
			if (Open & FClassicLattice::OpenRight)
			{
				const FIntPoint Door(Min.X + CorridorWidth, Min.Y);
				Layout.FillRect(Door, Door + FIntPoint(WallThickness, CorridorHeight), EMazeCellType::Empty);
			}

			// Up: a hole in the slab above, and a ladder standing on this cell's floor beneath it.
			if (Open & FClassicLattice::OpenUp)
			{
				const int32 HoleX = (R % 2 == 0) ? Min.X : Min.X + CorridorWidth - Hole;
				const FIntPoint HoleMin(HoleX, Min.Y + CorridorHeight);
				Layout.FillRect(HoleMin, HoleMin + FIntPoint(Hole, FloorThickness), EMazeCellType::Empty);
				ProtectRect(FIntPoint(HoleX, Min.Y - 1),
					FIntPoint(HoleX + Hole, Min.Y + PitchZ + FMath::Max(1, AgentHeightCells)));

				if (!LadderType.IsNone())
				{
					FMazePlacement Ladder;
					Ladder.TypeId = LadderType;
					Ladder.CellXZ = FIntPoint(HoleX + (Hole - 1) / 2, Min.Y);
					Ladder.Band = EMazeDepthBand::Play;
					Ladder.Anchor = EMazeAnchorKind::Floor;
					AddStructuralPlacement(Ladder);
				}
				++Ladders;
			}
		}
	}

	// 3. Dead ends and erosion happen in WriteLayout, the same for every generator.
	WriteLayout(Layout, InOutGrid, Rng);

	// 4. Where to put the start and the finish: the generator does not place transitions, but it
	//    knows where the longest walk ends.
	int32 Steps = 0;
	const FIntPoint Far = FindFarthest(Lattice, Steps);
	const FIntPoint StartCell = CellMin(0, 0);
	const FIntPoint FarCell = CellMin(Far.X, Far.Y);

	UE_LOG(LogMazeForge, Log,
		TEXT("Classic: %dx%d cells, %d loop(s), %d hole(s) with %s. Ladders must span %d cells "
		     "(Corridor Height + Floor Thickness). Start at grid cell (%d, %d); the farthest cell, "
		     "%d steps away, at (%d, %d)."),
		Cols, Rows, Loops, Ladders,
		LadderType.IsNone() ? TEXT("NO ladders — Ladder Type is empty")
		                    : *FString::Printf(TEXT("ladders of type '%s'"), *LadderType.ToString()),
		PitchZ, StartCell.X, StartCell.Y, Steps, FarCell.X, FarCell.Y);
}

#undef LOCTEXT_NAMESPACE
