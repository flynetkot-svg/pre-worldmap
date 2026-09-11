#include "Generators/MazeGenerator_Random.h"

#include "Data/MazeGrid.h"
#include "Generators/MazeLayout2D.h"
#include "MazeForgeCore.h"

#define LOCTEXT_NAMESPACE "MazeForge"

FText UMazeGenerator_Random::GetDisplayName() const
{
	return LOCTEXT("GeneratorRandom", "Random");
}

// ------------------------------------------------------------------------ sectors

void UMazeGenerator_Random::SplitSectors(const FSector& Sector, int32 Depth, FRandomStream& Rng,
                                         TArray<FSector>& OutSectors) const
{
	const int32 MinSide = FMath::Max(6, MinSectorCells);

	// We split only if both halves stay at or above the minimum. Otherwise the BSP
	// breeds one-cell strips out of which no hall can be carved any more.
	const bool bCanSplitX = Sector.Width()  >= MinSide * 2;
	const bool bCanSplitZ = Sector.Height() >= MinSide * 2;

	if (Depth >= MaxSplitDepth || (!bCanSplitX && !bCanSplitZ))
	{
		OutSectors.Add(Sector);
		return;
	}

	// We cut the long side: that way sectors tend towards a square rather than corridors.
	bool bSplitAlongX = bCanSplitX;
	if (bCanSplitX && bCanSplitZ)
	{
		bSplitAlongX = Sector.Width() >= Sector.Height();
	}

	FSector First = Sector;
	FSector Second = Sector;

	if (bSplitAlongX)
	{
		const int32 Cut = Rng.RandRange(Sector.Min.X + MinSide, Sector.Max.X - MinSide);
		First.Max.X = Cut;
		Second.Min.X = Cut;
	}
	else
	{
		const int32 Cut = Rng.RandRange(Sector.Min.Y + MinSide, Sector.Max.Y - MinSide);
		First.Max.Y = Cut;
		Second.Min.Y = Cut;
	}

	SplitSectors(First, Depth + 1, Rng, OutSectors);
	SplitSectors(Second, Depth + 1, Rng, OutSectors);
}

// -------------------------------------------------------------------------- halls

void UMazeGenerator_Random::CarveRooms(FMazeLayout2D& Layout, const TArray<FSector>& Sectors,
                                       FRandomStream& Rng, TArray<FSector>& OutRooms) const
{
	const int32 Margin = FMath::Max(1, RoomMargin);

	for (const FSector& Sector : Sectors)
	{
		if (Rng.FRand() > RoomChance)
		{
			continue;
		}

		FSector Room;
		Room.Min = FIntPoint(Sector.Min.X + Margin, Sector.Min.Y + Margin);
		Room.Max = FIntPoint(Sector.Max.X - Margin, Sector.Max.Y - Margin);

		// A hall must fit at least one floor in height, otherwise it is a slit.
		if (Room.Width() < 4 || Room.Height() < FloorGapMin + 1)
		{
			continue;
		}

		Layout.FillRect(Room.Min, Room.Max, EMazeCellType::Empty);
		OutRooms.Add(Room);
	}
}

// ------------------------------------------------------------------------- floors

void UMazeGenerator_Random::CarveFloors(FMazeLayout2D& Layout, const TArray<FSector>& Rooms,
                                        FRandomStream& Rng) const
{
	const int32 Thickness = FMath::Max(1, FloorThickness);
	const int32 Gap = FMath::Max(2, FloorGapMin);
	const int32 Step = Gap + Thickness;

	for (const FSector& Room : Rooms)
	{
		const int32 MaxFloorsByHeight = (Room.Height() - Gap) / Step;
		if (MaxFloorsByHeight <= 0)
		{
			continue;
		}

		const int32 Wanted = Rng.RandRange(FMath::Min(FloorsMin, FloorsMax),
		                                   FMath::Max(FloorsMin, FloorsMax));
		const int32 Count = FMath::Min(Wanted, MaxFloorsByHeight);

		for (int32 Index = 1; Index <= Count; ++Index)
		{
			const int32 FloorZ = Room.Min.Y + Index * Step - Thickness;
			if (FloorZ <= Room.Min.Y || FloorZ + Thickness >= Room.Max.Y)
			{
				continue;
			}

			Layout.FillRect(FIntPoint(Room.Min.X, FloorZ),
			                FIntPoint(Room.Max.X, FloorZ + Thickness),
			                EMazeCellType::Floor);

			if (Rng.FRand() > LadderChance)
			{
				continue;
			}

			// The opening in the slab. At least two cells wide: the player will not land
			// in a one-cell-wide slit while falling.
			const int32 HoleWidth = FMath::Min(FMath::Max(2, LadderOpeningCells), Room.Width());
			const int32 HoleX = Rng.RandRange(Room.Min.X, Room.Max.X - HoleWidth);

			Layout.FillRect(FIntPoint(HoleX, FloorZ),
			                FIntPoint(HoleX + HoleWidth, FloorZ + Thickness),
			                EMazeCellType::Empty);

			// The opening is the whole of it. A ladder used to be marked in the column below
			// as well, and that markup is gone: the grid holds mass, and a ladder is an object
			// the spawner places. The hole a ladder needs is cut here either way.
		}
	}
}

// ------------------------------------------------------------------------- shafts

void UMazeGenerator_Random::CarveShafts(FMazeLayout2D& Layout, const FSector& Area, int32 RoofZ,
                                        FRandomStream& Rng, TArray<int32>& OutShaftX) const
{
	const int32 Width = FMath::Max(1, ShaftWidth);
	const int32 Spacing = FMath::Max(Width + 1, ShaftMinSpacing);

	// A shaft lives strictly inside the working area: down to the bottom edge, but not
	// into the world border — otherwise the player falls through the floor of the map.
	const int32 BottomZ = Area.Min.Y;
	const int32 TopZ = bShaftsReachTop ? Area.Max.Y : FMath::Max(BottomZ + 1, RoofZ);

	// We try candidates with a margin: some of them get rejected by the minimum distance.
	const int32 Attempts = FMath::Max(1, ShaftCount) * 8;

	for (int32 Attempt = 0; Attempt < Attempts && OutShaftX.Num() < ShaftCount; ++Attempt)
	{
		if (Area.Max.X - Width <= Area.Min.X)
		{
			break;
		}

		const int32 X = Rng.RandRange(Area.Min.X, Area.Max.X - Width);

		const bool bTooClose = OutShaftX.ContainsByPredicate([X, Spacing](int32 Existing)
		{
			return FMath::Abs(Existing - X) < Spacing;
		});

		if (bTooClose)
		{
			continue;
		}

		// The shaft is cut after the floors and goes through the slabs — that is its point.
		Layout.FillRect(FIntPoint(X, BottomZ), FIntPoint(X + Width, TopZ), EMazeCellType::Empty);

		// The shaft itself and no ladder markup along its wall — see CarveFloors.
		OutShaftX.Add(X);
	}

	OutShaftX.Sort();
}

// ----------------------------------------------------------------------- passages

void UMazeGenerator_Random::CarveCorridors(FMazeLayout2D& Layout, const TArray<FSector>& Rooms,
                                           const TArray<int32>& ShaftX) const
{
	if (ShaftX.Num() == 0)
	{
		return;
	}

	// A passage cannot be lower than the player, whatever CorridorHeight is in the settings.
	const int32 Height = FMath::Max(FMath::Max(2, AgentHeightCells), CorridorHeight);
	const int32 Thickness = FMath::Max(1, FloorThickness);
	const int32 Gap = FMath::Max(2, FloorGapMin);
	const int32 Step = Gap + Thickness;

	for (const FSector& Room : Rooms)
	{
		// The nearest shaft horizontally: that is where we run a passage from every floor.
		int32 NearestX = ShaftX[0];
		int32 BestDistance = MAX_int32;

		const int32 RoomCenterX = (Room.Min.X + Room.Max.X) / 2;
		for (int32 Candidate : ShaftX)
		{
			const int32 Distance = FMath::Abs(Candidate - RoomCenterX);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				NearestX = Candidate;
			}
		}

		const int32 FromX = FMath::Min(NearestX, Room.Min.X);
		const int32 ToX = FMath::Max(NearestX + 1, Room.Max.X);

		// The passage runs along the floor of every storey, including the bottom level
		// of the hall.
		//
		// We cut all the way through, not only through stone: this used to be
		// FillRectIfSolid, and a slab of somebody else's hall in the way cut the corridor
		// off. From the outside it looked whole, while the player ran into a wall — those
		// were your unreachable halls.
		for (int32 FloorZ = Room.Min.Y; FloorZ + Height < Room.Max.Y; FloorZ += Step)
		{
			Layout.FillRect(FIntPoint(FromX, FloorZ),
			                FIntPoint(ToX, FloorZ + Height),
			                EMazeCellType::Empty);
		}
	}
}

// ----------------------------------------------------- entry point and connectivity

FIntPoint UMazeGenerator_Random::FindEntryPoint(const FMazeLayout2D& Layout, int32 RoofZ) const
{
	const int32 CenterX = Layout.Width() / 2;

	// We search from the top down: the player enters the maze from the roof, and the entry
	// point must be the topmost passable cell, not the first one that turns up.
	for (int32 Z = FMath::Min(RoofZ, Layout.Height() - 1); Z >= 0; --Z)
	{
		// Within a tier we take the cell closer to the middle of the map. The leftmost one
		// would drive the entry into a corner, from which half the maze is a long way back.
		int32 BestX = INDEX_NONE;
		int32 BestDistance = MAX_int32;

		for (int32 X = 0; X < Layout.Width(); ++X)
		{
			// CanStand specifically, not "the cell is free": the entry point must fit the
			// player whole. Otherwise the flood fill starts from a position the player is
			// not in, returns zero — and the whole maze turns out to be "unreachable".
			if (!Layout.CanStand(X, Z, FMath::Max(1, AgentHeightCells)))
			{
				continue;
			}

			const int32 Distance = FMath::Abs(X - CenterX);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				BestX = X;
			}
		}

		if (BestX != INDEX_NONE)
		{
			return FIntPoint(BestX, Z);
		}
	}

	return FIntPoint(INDEX_NONE, INDEX_NONE);
}

void UMazeGenerator_Random::CarveLine(FMazeLayout2D& Layout, const FIntPoint& From,
                                      const FIntPoint& To) const
{
	// An L-shaped run: first along X, then along Z. A diagonal cut is impassable in a
	// side-scroller — the player cannot move diagonally.
	//
	// The cut is carved to the agent's full size. This used to be a one-cell line: a
	// per-cell flood fill counted it as a passage, while the player did not fit into it.
	const int32 AgentHeight = FMath::Max(1, AgentHeightCells);
	const int32 OpeningWidth = FMath::Max(2, LadderOpeningCells);

	const int32 StepX = From.X <= To.X ? 1 : -1;
	for (int32 X = From.X; X != To.X + StepX; X += StepX)
	{
		Layout.FillRect(FIntPoint(X, From.Y), FIntPoint(X + 1, From.Y + AgentHeight),
			EMazeCellType::Empty);
	}

	// The vertical stretch is a well one has to land in while falling, so it is wider
	// than one cell for the same reasons as the opening in a slab.
	const int32 WellMinX = FMath::Min(To.X, To.X + OpeningWidth - 1);
	const int32 StepZ = From.Y <= To.Y ? 1 : -1;

	for (int32 Z = From.Y; Z != To.Y + StepZ; Z += StepZ)
	{
		Layout.FillRect(FIntPoint(WellMinX, Z), FIntPoint(WellMinX + OpeningWidth, Z + 1),
			EMazeCellType::Empty);
	}

	// A landing at the end of the well: without it the agent bumps its head on the
	// ceiling of the passage.
	Layout.FillRect(FIntPoint(WellMinX, To.Y),
		FIntPoint(WellMinX + OpeningWidth, To.Y + AgentHeight), EMazeCellType::Empty);
}

int32 UMazeGenerator_Random::EnsureConnectivity(FMazeLayout2D& Layout, const FIntPoint& Entry) const
{
	const int32 AgentHeight = FMath::Max(1, AgentHeightCells);

	TBitArray<> Standable;
	TBitArray<> Occupied;
	Layout.FloodFill(Entry, AgentHeight, Standable);
	Layout.BuildOccupied(Standable, AgentHeight, Occupied);

	TArray<FIntPoint> Seeds;
	TArray<int32> Sizes;
	Layout.FindIslands(Standable, AgentHeight, Seeds, Sizes);

	int32 Handled = 0;

	for (int32 Index = 0; Index < Seeds.Num(); ++Index)
	{
		// The name is deliberately not Seed: that is what the generation seed in the base
		// class is called, and a local variable would shadow it.
		const FIntPoint IslandSeed = Seeds[Index];

		// A previous cut may have attached this cavity as well. In that case it must not
		// be filled in under any circumstances — it is already part of the main volume,
		// and we would be walling up a piece of passable maze.
		if (Standable[Layout.Index(IslandSeed.X, IslandSeed.Y)])
		{
			continue;
		}

		const bool bTooSmall = Sizes[Index] < MinIslandCells;

		if (!bCarveIslands || bTooSmall)
		{
			Layout.FloodSolidBounded(IslandSeed, Occupied);
			++Handled;
			continue;
		}

		const FIntPoint Target = Layout.FindNearestReachable(IslandSeed, Standable);
		if (Target.X == INDEX_NONE)
		{
			Layout.FloodSolidBounded(IslandSeed, Occupied);
			++Handled;
			continue;
		}

		CarveLine(Layout, IslandSeed, Target);
		++Handled;

		// The cut attached a whole cavity, so the reachability map has to be recomputed:
		// the next islands may already be connected through it.
		Layout.FloodFill(Entry, AgentHeight, Standable);
		Layout.BuildOccupied(Standable, AgentHeight, Occupied);
	}

	return Handled;
}

// ----------------------------------------------------------------------- assembly

void UMazeGenerator_Random::Generate(FMazeGrid& InOutGrid, FRandomStream& Rng)
{
	const FIntPoint Size = InOutGrid.SizeXZ;
	if (Size.X < 8 || Size.Y < 8)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Random: the %dx%d grid is too small to generate in."), Size.X, Size.Y);
		return;
	}

	FMazeLayout2D Layout(Size);
	Layout.Fill(EMazeCellType::Solid);

	// The working area is inset by the world borders: the grid itself draws them through
	// IsBorderCell, and passages must not be cut into them — the player would fall out.
	const int32 Border = InOutGrid.Borders.ThicknessCells;
	const int32 InsetLeft   = InOutGrid.Borders.bCloseLeft   ? Border : 0;
	const int32 InsetRight  = InOutGrid.Borders.bCloseRight  ? Border : 0;
	const int32 InsetBottom = InOutGrid.Borders.bCloseBottom ? Border : 0;
	const int32 InsetTop    = InOutGrid.Borders.bCloseTop    ? Border : 0;

	FSector Root;
	Root.Min = FIntPoint(InsetLeft, InsetBottom);
	Root.Max = FIntPoint(Size.X - InsetRight, Size.Y - InsetTop);

	if (Root.Width() < 8 || Root.Height() < 8)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Random: after the border inset the %dx%d working area is too small."),
			Root.Width(), Root.Height());
		return;
	}

	// 1. Sectors and halls.
	TArray<FSector> Sectors;
	SplitSectors(Root, 0, Rng, Sectors);

	TArray<FSector> Rooms;
	CarveRooms(Layout, Sectors, Rng, Rooms);

	// 2. Floors inside the halls. Before the shafts: a shaft must punch through a slab.
	CarveFloors(Layout, Rooms, Rng);

	// 3. The roof corridor — the landing area and the link between the tops of the shafts.
	const int32 RoofZ = Root.Max.Y - FMath::Max(2, CorridorHeight);
	if (bCarveRoofCorridor && RoofZ > Root.Min.Y)
	{
		Layout.FillRect(FIntPoint(Root.Min.X, RoofZ), FIntPoint(Root.Max.X, Root.Max.Y),
			EMazeCellType::Empty);
	}

	// 4. Shafts.
	TArray<int32> ShaftX;
	CarveShafts(Layout, Root, RoofZ, Rng, ShaftX);

	// 5. Passages from the halls to the nearest shaft.
	CarveCorridors(Layout, Rooms, ShaftX);

	// 6. Entry point and connectivity.
	const FIntPoint Entry = FindEntryPoint(Layout, Root.Max.Y - 1);
	int32 IslandsHandled = 0;

	if (Entry.X != INDEX_NONE)
	{
		IslandsHandled = EnsureConnectivity(Layout, Entry);

		// The entry point is no longer written into the grid as a cell. It is reported in the
		// log below, and where the player actually starts is the spawner's business.
	}
	else
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Random: not a single passable cell was found — check the hall parameters."));
	}

	// 7. A control check of the result.
	//
	// The generator must not silently release a maze with unreachable halls: the designer
	// would only spot them by eye, having already sliced and exported the map.
	int32 LeftoverIslands = 0;
	if (Entry.X != INDEX_NONE)
	{
		const int32 AgentHeight = FMath::Max(1, AgentHeightCells);

		TBitArray<> Standable;
		Layout.FloodFill(Entry, AgentHeight, Standable);

		TArray<FIntPoint> Leftover;
		TArray<int32> LeftoverSizes;
		Layout.FindIslands(Standable, AgentHeight, Leftover, LeftoverSizes);

		LeftoverIslands = Leftover.Num();

		if (LeftoverIslands > 0)
		{
			UE_LOG(LogMazeForge, Warning,
				TEXT("Random: %d unreachable cavities are left. The first one is at X %d Z %d. ")
				TEXT("Increase ShaftCount or CorridorHeight, or reduce AgentHeightCells."),
				LeftoverIslands, Leftover[0].X, Leftover[0].Y);
		}
	}

	// 8. The flat solution is stretched along the depth.
	MazeWriteLayoutToGrid(Layout, InOutGrid);

	UE_LOG(LogMazeForge, Log,
		TEXT("Random: %d sectors, %d halls, %d shafts, %d cavities handled, ")
		TEXT("%d unreachable left, entry at X %d Z %d."),
		Sectors.Num(), Rooms.Num(), ShaftX.Num(), IslandsHandled, LeftoverIslands,
		Entry.X, Entry.Y);
}

#undef LOCTEXT_NAMESPACE
