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

		/** Cells that are corridor. Every one, except in Path First, where the unused stay solid. */
		TBitArray<> Used;

		static constexpr uint8 OpenRight = 1;
		static constexpr uint8 OpenUp = 2;

		FClassicLattice(int32 InCols, int32 InRows) : Cols(InCols), Rows(InRows)
		{
			Open.SetNumZeroed(Cols * Rows);
			Used.Init(true, Cols * Rows);
		}

		bool IsUsed(const FIntPoint& P) const { return IsInside(P.X, P.Y) && Used[Index(P.X, P.Y)]; }

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
					if (!L.IsUsed(A) || !L.IsUsed(B) || L.IsLinked(A, B))
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
	FIntPoint FindFarthest(const FClassicLattice& L, const FIntPoint& Start, int32& OutSteps)
	{
		TArray<int32> Distance;
		Distance.Init(INDEX_NONE, L.Cols * L.Rows);
		TArray<FIntPoint> Queue;
		Queue.Add(Start);
		Distance[L.Index(Start.X, Start.Y)] = 0;

		FIntPoint Farthest = Start;
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

	struct FPathFirstParams
	{
		float Winding = 0.5f;
		float VerticalBias = 0.35f;
		int32 Branches = 0;
		int32 DepthMin = 1;
		int32 DepthMax = 1;
		float SubChance = 0.0f;
		int32 MaxNesting = 1;
		bool bFillLeftover = false;

		/** Lattice cells. Main: through these in order; empty — start bottom-left, exit top-right. */
		TArray<FIntPoint> MainPoints;
		TArray<TArray<FIntPoint>> BranchPoints;
	};

	struct FPathFirstOut
	{
		FIntPoint Start = FIntPoint::ZeroValue;
		FIntPoint Exit = FIntPoint::ZeroValue;
		int32 MainCells = 0;
		int32 Branches = 0;
		int32 SubBranches = 0;
		int32 Planned = 0;
		int32 Leftover = 0;
		int32 Missed = 0;
	};

	/**
	 *  Walks from From to To through unused cells and joins them. Depth-first with backtracking,
	 *  so it always gets there if anything can: at each step, with probability 1 - Winding, the
	 *  neighbour closest to the target; otherwise a random one. To may already be used.
	 */
	bool Connect(FClassicLattice& L, const FIntPoint& From, const FIntPoint& To, float Winding,
	             float VerticalBias, FRandomStream& Rng, TArray<FIntPoint>* OutPath)
	{
		if (From == To)
		{
			return true;
		}

		TBitArray<> Seen = L.Used;
		TArray<FIntPoint> Stack{ From };
		Seen[L.Index(From.X, From.Y)] = true;
		TArray<FIntPoint> Options;

		while (Stack.Num() > 0 && Stack.Last() != To)
		{
			const FIntPoint Cell = Stack.Last();
			Options.Reset();
			bool bTargetNext = false;
			for (const FIntPoint& Dir : Directions)
			{
				const FIntPoint N = Cell + Dir;
				if (!L.IsInside(N.X, N.Y))
				{
					continue;
				}
				if (N == To)
				{
					bTargetNext = true;
				}
				else if (!Seen[L.Index(N.X, N.Y)])
				{
					Options.Add(N);
				}
			}

			if (bTargetNext)
			{
				Stack.Add(To);
				break;
			}
			if (Options.Num() == 0)
			{
				Stack.Pop();
				continue;
			}

			FIntPoint Next;
			if (Rng.FRand() >= Winding)
			{
				Options.Sort([&To](const FIntPoint& A, const FIntPoint& B)
				{
					return FMath::Abs(A.X - To.X) + FMath::Abs(A.Y - To.Y) < FMath::Abs(B.X - To.X) + FMath::Abs(B.Y - To.Y);
				});
				Next = Options[0];
			}
			else
			{
				Next = Options[PickWeighted(Options,
					[&Cell, VerticalBias](const FIntPoint& N) { return Weight(N - Cell, VerticalBias); }, Rng)];
			}

			Seen[L.Index(Next.X, Next.Y)] = true;
			Stack.Add(Next);
		}

		if (Stack.Num() == 0 || Stack.Last() != To)
		{
			return false;
		}

		for (int32 I = 1; I < Stack.Num(); ++I)
		{
			L.Link(Stack[I - 1], Stack[I]);
			L.Used[L.Index(Stack[I].X, Stack[I].Y)] = true;
		}
		if (OutPath)
		{
			OutPath->Append(Stack.GetData() + 1, Stack.Num() - 1);
		}
		return true;
	}

	/** A random walk into unused cells, up to Length. Returns the cells it took. */
	TArray<FIntPoint> Grow(FClassicLattice& L, const FIntPoint& From, int32 Length, float VerticalBias, FRandomStream& Rng)
	{
		TArray<FIntPoint> Cells;
		FIntPoint Cell = From;
		TArray<FIntPoint> Options;
		for (int32 Step = 0; Step < Length; ++Step)
		{
			Options.Reset();
			for (const FIntPoint& Dir : Directions)
			{
				const FIntPoint N = Cell + Dir;
				if (L.IsInside(N.X, N.Y) && !L.IsUsed(N))
				{
					Options.Add(N);
				}
			}
			if (Options.Num() == 0)
			{
				break;
			}

			const FIntPoint Next = Options[PickWeighted(Options,
				[&Cell, VerticalBias](const FIntPoint& N) { return Weight(N - Cell, VerticalBias); }, Rng)];
			L.Link(Cell, Next);
			L.Used[L.Index(Next.X, Next.Y)] = true;
			Cells.Add(Next);
			Cell = Next;
		}
		return Cells;
	}

	/** A branch and, by chance, branches off it, down to MaxNesting. */
	void GrowBranch(FClassicLattice& L, const FIntPoint& From, int32 Level, const FPathFirstParams& P,
	                FRandomStream& Rng, FPathFirstOut& Out)
	{
		const int32 Scale = Level == 1 ? 1 : 2;
		const int32 Min = FMath::Max(1, P.DepthMin / Scale);
		const int32 Max = FMath::Max(Min, P.DepthMax / Scale);
		const TArray<FIntPoint> Cells = Grow(L, From, Rng.RandRange(Min, Max), P.VerticalBias, Rng);
		if (Cells.Num() == 0)
		{
			return;
		}

		(Level == 1 ? Out.Branches : Out.SubBranches) += 1;
		if (Level >= P.MaxNesting)
		{
			return;
		}

		for (int32 I = 0; I + 1 < Cells.Num(); ++I)
		{
			if (Rng.FRand() < P.SubChance)
			{
				GrowBranch(L, Cells[I], Level + 1, P, Rng, Out);
			}
		}
	}

	void CarvePathFirst(FClassicLattice& L, const FPathFirstParams& P, FRandomStream& Rng, FPathFirstOut& Out)
	{
		L.Used.Init(false, L.Cols * L.Rows);
		TBitArray<> OnMain(false, L.Cols * L.Rows);
		TArray<FIntPoint> Main;

		// --- the main path
		const bool bPlanned = P.MainPoints.Num() >= 2;
		Out.Start = bPlanned ? P.MainPoints[0] : FIntPoint(0, 0);
		L.Used[L.Index(Out.Start.X, Out.Start.Y)] = true;
		Main.Add(Out.Start);

		FIntPoint Cell = Out.Start;
		const TArray<FIntPoint> Targets = bPlanned
			? TArray<FIntPoint>(P.MainPoints.GetData() + 1, P.MainPoints.Num() - 1)
			: TArray<FIntPoint>{ FIntPoint(L.Cols - 1, L.Rows - 1) };

		for (const FIntPoint& Target : Targets)
		{
			if (Target == Cell)
			{
				continue;
			}
			if (Connect(L, Cell, Target, P.Winding, P.VerticalBias, Rng, &Main))
			{
				Cell = Target;
			}
			else
			{
				++Out.Missed;
			}
		}
		Out.Exit = Cell;
		Out.MainCells = Main.Num();
		for (const FIntPoint& M : Main)
		{
			OnMain[L.Index(M.X, M.Y)] = true;
		}

		// --- branches planned by hand: off the nearest corridor already made
		for (const TArray<FIntPoint>& Points : P.BranchPoints)
		{
			if (Points.Num() == 0)
			{
				continue;
			}

			FIntPoint Anchor = Out.Start;
			int32 Best = MAX_int32;
			for (int32 R = 0; R < L.Rows; ++R)
			{
				for (int32 C = 0; C < L.Cols; ++C)
				{
					const int32 D = FMath::Square(C - Points[0].X) + FMath::Square(R - Points[0].Y);
					if (L.Used[L.Index(C, R)] && D < Best)
					{
						Best = D;
						Anchor = FIntPoint(C, R);
					}
				}
			}

			bool bAny = false;
			FIntPoint From = Anchor;
			for (const FIntPoint& Point : Points)
			{
				if (Connect(L, From, Point, P.Winding * 0.5f, P.VerticalBias, Rng, nullptr))
				{
					From = Point;
					bAny = true;
				}
				else
				{
					++Out.Missed;
				}
			}
			Out.Planned += bAny ? 1 : 0;
		}

		// --- random branches, spread along the main path
		if (P.Branches > 0 && Main.Num() > 2)
		{
			const float Spacing = static_cast<float>(Main.Num()) / (P.Branches + 1);
			for (int32 B = 0; B < P.Branches; ++B)
			{
				int32 Index = FMath::Clamp(FMath::RoundToInt(Spacing * (B + 1) + Rng.FRandRange(-Spacing * 0.3f, Spacing * 0.3f)),
					1, Main.Num() - 2);

				// The nearest main-path cell from there on that still has room beside it.
				for (int32 Tries = 0; Tries < Main.Num(); ++Tries, Index = 1 + (Index % (Main.Num() - 2)))
				{
					const FIntPoint At = Main[Index];
					const bool bRoom = [&L, &At]()
					{
						for (const FIntPoint& Dir : Directions)
						{
							const FIntPoint N = At + Dir;
							if (L.IsInside(N.X, N.Y) && !L.IsUsed(N))
							{
								return true;
							}
						}
						return false;
					}();
					if (bRoom)
					{
						GrowBranch(L, At, 1, P, Rng, Out);
						break;
					}
				}
			}
		}

		// --- the rest as short spurs off anything but the main path
		if (P.bFillLeftover)
		{
			for (const bool bAllowMain : { false, true })
			{
				TArray<TPair<FIntPoint, FIntPoint>> Frontier;
				for (int32 R = 0; R < L.Rows; ++R)
				{
					for (int32 C = 0; C < L.Cols; ++C)
					{
						const FIntPoint A(C, R);
						if (!L.IsUsed(A) || (!bAllowMain && OnMain[L.Index(C, R)]))
						{
							continue;
						}
						for (const FIntPoint& Dir : Directions)
						{
							const FIntPoint N = A + Dir;
							if (L.IsInside(N.X, N.Y) && !L.IsUsed(N))
							{
								Frontier.Add({ A, N });
							}
						}
					}
				}

				while (Frontier.Num() > 0)
				{
					const int32 Pick = Rng.RandRange(0, Frontier.Num() - 1);
					const TPair<FIntPoint, FIntPoint> Edge = Frontier[Pick];
					Frontier.RemoveAtSwap(Pick);
					if (L.IsUsed(Edge.Value))
					{
						continue;
					}

					L.Link(Edge.Key, Edge.Value);
					L.Used[L.Index(Edge.Value.X, Edge.Value.Y)] = true;
					++Out.Leftover;
					for (const FIntPoint& Dir : Directions)
					{
						const FIntPoint N = Edge.Value + Dir;
						if (L.IsInside(N.X, N.Y) && !L.IsUsed(N))
						{
							Frontier.Add({ Edge.Value, N });
						}
					}
				}
			}
		}
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
	if (DeadEndChance > 0.0f && DeadEndMaxHeight > 0 && CorridorHeight > DeadEndMaxHeight)
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
	FPathFirstOut PathFirst;
	FIntPoint StartLattice(0, 0);

	// Grid cell to the corridor cell it falls in.
	auto ToLattice = [&](const FIntPoint& Cell)
	{
		return FIntPoint(
			FMath::Clamp(FMath::FloorToInt(float(Cell.X - AreaMin.X - WallThickness) / PitchX), 0, Cols - 1),
			FMath::Clamp(FMath::FloorToInt(float(Cell.Y - AreaMin.Y - FloorThickness) / PitchZ), 0, Rows - 1));
	};

	if (Style == EMazeClassicStyle::PathFirst)
	{
		FPathFirstParams P;
		P.Winding = MainPathWinding;
		P.VerticalBias = VerticalBias;
		P.Branches = Branches;
		P.DepthMin = BranchDepthMin;
		P.DepthMax = FMath::Max(BranchDepthMin, BranchDepthMax);
		P.SubChance = SubBranchChance;
		P.MaxNesting = MaxNesting;
		P.bFillLeftover = bFillLeftover;
		for (const FIntPoint& Point : PlannedMainPath)
		{
			const FIntPoint L = ToLattice(Point);
			if (P.MainPoints.Num() == 0 || P.MainPoints.Last() != L)
			{
				P.MainPoints.Add(L);
			}
		}
		for (const FMazePlannedBranch& Branch : PlannedBranches)
		{
			TArray<FIntPoint>& Points = P.BranchPoints.AddDefaulted_GetRef();
			for (const FIntPoint& Point : Branch.Points)
			{
				Points.Add(ToLattice(Point));
			}
		}

		CarvePathFirst(Lattice, P, Rng, PathFirst);
		StartLattice = PathFirst.Start;
	}
	else if (Style == EMazeClassicStyle::Branchy)
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
			if (!Lattice.Used[Lattice.Index(C, R)])
			{
				continue; // Path First leaves what it did not use as mass
			}

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
	const FIntPoint Far = FindFarthest(Lattice, StartLattice, Steps);
	const FIntPoint StartCell = CellMin(StartLattice.X, StartLattice.Y);
	const FIntPoint FarCell = CellMin(Far.X, Far.Y);

	if (Style == EMazeClassicStyle::PathFirst)
	{
		const FIntPoint ExitCell = CellMin(PathFirst.Exit.X, PathFirst.Exit.Y);

		// The ends of the main path, as points the analysis and the game both read.
		auto Mark = [this](FName Type, const FIntPoint& Cell, EMazeAnchorKind Anchor)
		{
			if (!Type.IsNone())
			{
				FMazePlacement Marker;
				Marker.TypeId = Type;
				Marker.CellXZ = Cell;
				Marker.Band = EMazeDepthBand::Play;
				Marker.Anchor = Anchor;
				AddStructuralPlacement(Marker);
			}
		};
		Mark(StartMarkerType, StartCell + FIntPoint(CorridorWidth / 2, 0), EMazeAnchorKind::Free);
		Mark(ExitMarkerType, ExitCell + FIntPoint(CorridorWidth / 2, 0), EMazeAnchorKind::Floor);

		UE_LOG(LogMazeForge, Log,
			TEXT("Classic, path first: main path %d corridor cells from (%d, %d) to (%d, %d)%s; %d branch(es), "
			     "%d sub-branch(es), %d planned branch(es), %d leftover cell(s) joined%s."),
			PathFirst.MainCells, StartCell.X, StartCell.Y, ExitCell.X, ExitCell.Y,
			PlannedMainPath.Num() >= 2 ? TEXT(" through the planned points") : TEXT(""),
			PathFirst.Branches, PathFirst.SubBranches, PathFirst.Planned, PathFirst.Leftover,
			PathFirst.Missed > 0
				? *FString::Printf(TEXT("; %d planned point(s) could not be reached through unused cells"), PathFirst.Missed)
				: TEXT(""));
	}

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
