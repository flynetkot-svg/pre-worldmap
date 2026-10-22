#include "Generators/MazeGenerator_Dungeon.h"

#include "Data/MazeGrid.h"
#include "Generators/MazeLayout2D.h"
#include "MazeForgeCore.h"

#define LOCTEXT_NAMESPACE "MazeForge"

UMazeGenerator_Dungeon::UMazeGenerator_Dungeon()
{
	// Rock wants rough edges; the base leaves them straight for everybody else.
	ErosionAmount = 0.35f;
}

FText UMazeGenerator_Dungeon::GetDisplayName() const
{
	return LOCTEXT("GeneratorDungeon", "Dungeon");
}

// ------------------------------------------------------------------- the split

int32 UMazeGenerator_Dungeon::SplitAndCarve(FMazeLayout2D& Layout, const FRect& Area, int32 Depth,
                                            FRandomStream& Rng,
                                            TArray<FChamber>& InOutChambers) const
{
	const int32 Wall = FMath::Max(1, WallThickness);

	// The smallest sector still worth splitting: a chamber needs its walls on both sides and
	// something between them. Measured per axis, because the two targets differ.
	const int32 MinSideX = 2 * Wall + 3;
	const int32 MinSideZ = 2 * Wall + FMath::Max(2, AgentHeightCells + 1);

	const int32 TargetX = FMath::Max(MinSideX, TargetChamberCells.X + 2 * Wall);
	const int32 TargetZ = FMath::Max(MinSideZ, TargetChamberCells.Y + 2 * Wall);

	// How far past its target each side is. Splitting halves the ratio, so a side at 1.5 of
	// its target is the point where halving stops overshooting in the other direction.
	const float OverX = static_cast<float>(Area.Width()) / static_cast<float>(TargetX);
	const float OverZ = static_cast<float>(Area.Height()) / static_cast<float>(TargetZ);

	// Both halves must survive the cut, which is a stricter condition than being over target.
	const bool bCanSplitX = Area.Width() >= MinSideX * 2 && OverX >= 1.5f;
	const bool bCanSplitZ = Area.Height() >= MinSideZ * 2 && OverZ >= 1.5f;

	if (Depth >= MaxSplitDepth || (!bCanSplitX && !bCanSplitZ))
	{
		const int32 Before = InOutChambers.Num();
		CarveChamber(Layout, Area, Rng, InOutChambers);

		return InOutChambers.Num() > Before ? InOutChambers.Num() - 1 : INDEX_NONE;
	}

	// The side that is furthest past ITS OWN target gets cut — not the longer side.
	//
	// Cutting the longer side is what a dungeon on a square map wants, and it is what the
	// first version did. On a map that is twice as wide as it is tall it produces chambers
	// that tend towards squares, and a square room in a side-scroller is a pit: the player
	// walks along the level, and every cell of height has to be climbed. Compared against
	// separate targets, a wide map simply gets more rooms across, each still low.
	bool bSplitAlongX = bCanSplitX;
	if (bCanSplitX && bCanSplitZ)
	{
		bSplitAlongX = OverX >= OverZ;
	}

	FRect First = Area;
	FRect Second = Area;

	// The cut wanders around the middle by the variance. Cutting exactly in half gives a
	// grid of identical rooms; cutting anywhere at all gives corridors next to halls.
	const float Spread = FMath::Clamp(ChamberVariance, 0.0f, 0.9f) * 0.5f;
	const float Fraction = 0.5f + Rng.FRandRange(-Spread, Spread);

	if (bSplitAlongX)
	{
		const int32 Span = Area.Width();
		const int32 Cut = Area.Min.X + FMath::Clamp(
			FMath::RoundToInt(Span * Fraction), MinSideX, Span - MinSideX);

		First.Max.X = Cut;
		Second.Min.X = Cut;
	}
	else
	{
		const int32 Span = Area.Height();
		const int32 Cut = Area.Min.Y + FMath::Clamp(
			FMath::RoundToInt(Span * Fraction), MinSideZ, Span - MinSideZ);

		First.Max.Y = Cut;
		Second.Min.Y = Cut;
	}

	const int32 A = SplitAndCarve(Layout, First, Depth + 1, Rng, InOutChambers);
	const int32 B = SplitAndCarve(Layout, Second, Depth + 1, Rng, InOutChambers);

	// One of the halves may be all rock, and then there is nothing to join.
	if (A == INDEX_NONE || B == INDEX_NONE)
	{
		return A != INDEX_NONE ? A : B;
	}

	if (bSplitAlongX)
	{
		CarveDoor(Layout, InOutChambers[A], InOutChambers[B], Rng);
	}
	else
	{
		// First is the lower half: the cut was made along Z and Min.Y is the bottom.
		CarveChimney(Layout, InOutChambers[A], InOutChambers[B], Rng);
	}

	// Either child stands for the subtree from here up. The lower one, so that the chain of
	// joins drifts downwards and the entry point ends up somewhere a player can start.
	return InOutChambers[A].FloorZ <= InOutChambers[B].FloorZ ? A : B;
}

// ---------------------------------------------------------------------- chamber

void UMazeGenerator_Dungeon::CarveChamber(FMazeLayout2D& Layout, const FRect& Area,
                                          FRandomStream& Rng,
                                          TArray<FChamber>& InOutChambers) const
{
	const int32 Wall = FMath::Max(1, WallThickness);

	FChamber Chamber;
	Chamber.Inner.Min = FIntPoint(Area.Min.X + Wall, Area.Min.Y + Wall);
	Chamber.Inner.Max = FIntPoint(Area.Max.X - Wall, Area.Max.Y - Wall);

	// A chamber has to hold a standing player and leave a cell of air above him, otherwise
	// it is a crack in the rock that happens to be rectangular.
	const int32 MinHeight = FMath::Max(2, AgentHeightCells + 1);
	if (Chamber.Inner.Width() < 3 || Chamber.Inner.Height() < MinHeight)
	{
		return;
	}

	Layout.FillRect(Chamber.Inner.Min, Chamber.Inner.Max, EMazeCellType::Empty);

	Chamber.FloorZ = Chamber.Inner.Min.Y;
	InOutChambers.Add(Chamber);

	CarveLedges(Layout, Chamber, Rng);
}

// ----------------------------------------------------------------------- ledges

void UMazeGenerator_Dungeon::CarveLedges(FMazeLayout2D& Layout, const FChamber& Chamber,
                                         FRandomStream& Rng) const
{
	const int32 Jump = FMath::Max(1, JumpHeightCells);
	const int32 Thickness = FMath::Max(1, LedgeThickness);
	const int32 Head = FMath::Max(1, AgentHeightCells);

	// One level to the next: the player stands on a ledge, has to clear the thickness of the
	// one above and still land on top of it. Anything more than a jump apart is a wall
	// disguised as a room.
	const int32 Step = Jump + Thickness;

	const int32 MinLength = FMath::Max(2, FMath::Min(LedgeMinLength, LedgeMaxLength));
	const int32 MaxLength = FMath::Max(MinLength, LedgeMaxLength);

	// Where the previous level's ledge was, so the next one can be made to overlap it. The
	// overlap is what turns two ledges into a staircase: without it they are two shelves at
	// opposite ends of a room, a jump apart in height and unreachable from each other.
	int32 PrevMinX = Chamber.Inner.Min.X;
	int32 PrevMaxX = Chamber.Inner.Max.X;

	for (int32 Z = Chamber.FloorZ + Step; Z + Thickness + Head <= Chamber.Inner.Max.Y; Z += Step)
	{
		if (Rng.FRand() > LedgeChance)
		{
			// Skipped, and the level above must now reach across two steps. Rather than let
			// that happen silently, the run of ledges restarts from the full width: the next
			// one spans the chamber and is reachable from anywhere below it.
			PrevMinX = Chamber.Inner.Min.X;
			PrevMaxX = Chamber.Inner.Max.X;
			continue;
		}

		const int32 Span = Chamber.Inner.Width();
		const int32 Length = FMath::Clamp(Rng.RandRange(MinLength, MaxLength), 2, Span);

		// The ledge must touch the span below it by at least one cell — that cell is where
		// the player jumps up.
		const int32 LowX = FMath::Max(Chamber.Inner.Min.X, PrevMinX - Length + 1);
		const int32 HighX = FMath::Min(Chamber.Inner.Max.X - Length, PrevMaxX - 1);

		const int32 StartX = HighX >= LowX
			? Rng.RandRange(LowX, HighX)
			: Chamber.Inner.Min.X;

		Layout.FillRect(FIntPoint(StartX, Z),
		                FIntPoint(StartX + Length, Z + Thickness),
		                EMazeCellType::Floor);

		PrevMinX = StartX;
		PrevMaxX = StartX + Length;
	}
}

// ------------------------------------------------------------------------ doors

void UMazeGenerator_Dungeon::CarveDoor(FMazeLayout2D& Layout, const FChamber& Left,
                                       const FChamber& Right, FRandomStream& Rng) const
{
	// Which one is actually on the left. The recursion knows, but saying it here means the
	// function is still correct the day somebody calls it from somewhere else.
	const FChamber& A = Left.Inner.Min.X <= Right.Inner.Min.X ? Left : Right;
	const FChamber& B = Left.Inner.Min.X <= Right.Inner.Min.X ? Right : Left;

	const int32 Height = FMath::Max(FMath::Max(1, AgentHeightCells), DoorHeightCells);

	// The doorway sits on the higher of the two floors: a door at the lower one would open
	// into the other chamber's wall. Both sides then have to have room for it.
	const int32 DoorZ = FMath::Max(A.FloorZ, B.FloorZ);

	if (DoorZ + Height > A.Inner.Max.Y || DoorZ + Height > B.Inner.Max.Y)
	{
		return;
	}

	Layout.FillRect(FIntPoint(A.Inner.Max.X, DoorZ),
	                FIntPoint(B.Inner.Min.X, DoorZ + Height),
	                EMazeCellType::Empty);

	// The chamber whose floor is lower now has a doorway in its wall partway up. A step
	// under it turns that into a way through; without it the door is a window.
	const FChamber& Lower = A.FloorZ < B.FloorZ ? A : B;
	const int32 Rise = DoorZ - Lower.FloorZ;

	if (Rise <= 0)
	{
		return;
	}

	const int32 Jump = FMath::Max(1, JumpHeightCells);
	const int32 Thickness = FMath::Max(1, LedgeThickness);
	const bool bLowerIsLeft = &Lower == &A;

	// Steps from the lower floor up to the door, each within a jump of the last, laid
	// against the wall the door is in.
	const int32 StepLength = FMath::Max(2, FMath::Min(Rng.RandRange(3, 5), Lower.Inner.Width()));

	for (int32 Z = Lower.FloorZ + Jump + Thickness; Z < DoorZ; Z += Jump + Thickness)
	{
		const int32 StartX = bLowerIsLeft
			? Lower.Inner.Max.X - StepLength
			: Lower.Inner.Min.X;

		Layout.FillRect(FIntPoint(StartX, Z),
		                FIntPoint(StartX + StepLength, Z + Thickness),
		                EMazeCellType::Floor);
	}
}

// --------------------------------------------------------------------- chimneys

void UMazeGenerator_Dungeon::CarveChimney(FMazeLayout2D& Layout, const FChamber& Lower,
                                          const FChamber& Upper, FRandomStream& Rng) const
{
	const FChamber& Bottom = Lower.Inner.Min.Y <= Upper.Inner.Min.Y ? Lower : Upper;
	const FChamber& Top = Lower.Inner.Min.Y <= Upper.Inner.Min.Y ? Upper : Lower;

	// Where the two chambers overlap along X — the chimney has to open into both, not into
	// the rock beside one of them.
	const int32 OverlapMin = FMath::Max(Bottom.Inner.Min.X, Top.Inner.Min.X);
	const int32 OverlapMax = FMath::Min(Bottom.Inner.Max.X, Top.Inner.Max.X);

	const int32 Width = FMath::Max(2, ChimneyWidthCells);
	if (OverlapMax - OverlapMin < Width)
	{
		return;
	}

	const int32 StartX = Rng.RandRange(OverlapMin, OverlapMax - Width);

	const int32 FromZ = Bottom.Inner.Max.Y;
	const int32 ToZ = Top.Inner.Min.Y;

	if (ToZ <= FromZ)
	{
		return;
	}

	Layout.FillRect(FIntPoint(StartX, FromZ), FIntPoint(StartX + Width, ToZ),
	                EMazeCellType::Empty);

	// A hole is not a way up. Steps go in alternating sides of the shaft, a jump apart, so
	// the player climbs it the way you climb a chimney — which is also why it is not one
	// cell wide: there would be nowhere to put them.
	const int32 Jump = FMath::Max(1, JumpHeightCells);
	const int32 Thickness = FMath::Max(1, LedgeThickness);
	const int32 StepLength = FMath::Max(1, Width / 2);

	bool bLeft = true;

	// From below the shaft to above it: the climb starts on the lower chamber's own ledges
	// and ends on the upper chamber's floor, so the steps have to span the wall between.
	for (int32 Z = FromZ - 1 + Jump + Thickness; Z + Thickness <= ToZ; Z += Jump + Thickness)
	{
		const int32 X = bLeft ? StartX : StartX + Width - StepLength;

		Layout.FillRect(FIntPoint(X, Z), FIntPoint(X + StepLength, Z + Thickness),
		                EMazeCellType::Floor);

		bLeft = !bLeft;
	}
}

// --------------------------------------------------------------------- erosion

// ------------------------------------------------------------------ entry point

FIntPoint UMazeGenerator_Dungeon::FindEntryPoint(const TArray<FChamber>& Chambers) const
{
	// The lowest chamber. Not the largest and not the first: a player dropped into a room
	// halfway up can reach everything below by falling, and nothing above it at all.
	const FChamber* Best = nullptr;

	for (const FChamber& Chamber : Chambers)
	{
		if (!Best || Chamber.FloorZ < Best->FloorZ)
		{
			Best = &Chamber;
		}
	}

	if (!Best)
	{
		return FIntPoint(INDEX_NONE, INDEX_NONE);
	}

	return FIntPoint(Best->Inner.Centre().X, Best->FloorZ);
}

// --------------------------------------------------------------------- pockets

int32 UMazeGenerator_Dungeon::FillPockets(FMazeLayout2D& Layout, const FIntPoint& Entry) const
{
	const int32 Height = FMath::Max(1, AgentHeightCells);

	TBitArray<> Standable;
	Layout.FloodFill(Entry, Height, Standable);

	TBitArray<> Occupied;
	Layout.BuildOccupied(Standable, Height, Occupied);

	TArray<FIntPoint> Seeds;
	TArray<int32> Sizes;
	Layout.FindIslands(Standable, Height, Seeds, Sizes);

	int32 Filled = 0;

	for (int32 Index = 0; Index < Seeds.Num(); ++Index)
	{
		// A big pocket is left where it is. It is almost always a chamber whose door did not
		// fit, and filling a room in solid is a worse answer than leaving one the designer
		// can see and deal with.
		if (Sizes[Index] > MaxPocketCells)
		{
			continue;
		}

		Filled += Layout.FloodSolidBounded(Seeds[Index], Occupied);
	}

	return Filled;
}

// -------------------------------------------------------------------- the whole

void UMazeGenerator_Dungeon::Generate(FMazeGrid& InOutGrid, FRandomStream& Rng)
{
	const FIntPoint Size = InOutGrid.SizeXZ;
	if (Size.X < 16 || Size.Y < 16)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Dungeon: the %dx%d grid is too small to put chambers in."), Size.X, Size.Y);
		return;
	}

	FMazeLayout2D Layout(Size);
	Layout.Fill(EMazeCellType::Solid);

	// Inset by the world borders: the grid draws them through IsBorderCell whether or not a
	// cell was ever painted, and a chamber carved into one is a room with a hole in the
	// floor of the world.
	const int32 Border = InOutGrid.Borders.ThicknessCells;

	FRect Root;
	Root.Min = FIntPoint(InOutGrid.Borders.bCloseLeft ? Border : 0,
	                     InOutGrid.Borders.bCloseBottom ? Border : 0);
	Root.Max = FIntPoint(Size.X - (InOutGrid.Borders.bCloseRight ? Border : 0),
	                     Size.Y - (InOutGrid.Borders.bCloseTop ? Border : 0));

	const int32 MinSide = 2 * FMath::Max(1, WallThickness) + 3;

	if (Root.Width() < MinSide || Root.Height() < MinSide)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Dungeon: after the border inset the working area is %dx%d, and a chamber "
			     "with walls %d thick needs at least %d on a side."),
			Root.Width(), Root.Height(), WallThickness, MinSide);
		return;
	}

	TArray<FChamber> Chambers;
	SplitAndCarve(Layout, Root, 0, Rng, Chambers);

	if (Chambers.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Dungeon: no chamber fitted in the %dx%d working area. Target Chamber Cells is "
			     "%dx%d and every chamber also needs %d cells of wall on each side."),
			Root.Width(), Root.Height(),
			TargetChamberCells.X, TargetChamberCells.Y, WallThickness);
		return;
	}

	// Erosion first, pockets after. The other order looks equivalent and is not: erosion is
	// the one step here that can seal a corner off, and the pocket pass is what notices. Run
	// it before and it certifies a layout that is about to change.
	const int32 Eroded = ShapeOutline(Layout, Root.Min, Root.Max, Rng);

	const FIntPoint Entry = FindEntryPoint(Chambers);

	int32 Filled = 0;
	if (bFillUnreachablePockets && Entry.X != INDEX_NONE)
	{
		Filled = FillPockets(Layout, Entry);
	}

	WriteLayout(Layout, InOutGrid, Rng);

	// The average size is reported because it is the number the target is aimed at, and the
	// only way to tell a target that was met from one that the minimum sizes overruled.
	int32 TotalW = 0;
	int32 TotalH = 0;
	for (const FChamber& Chamber : Chambers)
	{
		TotalW += Chamber.Inner.Width();
		TotalH += Chamber.Inner.Height();
	}

	UE_LOG(LogMazeForge, Log,
		TEXT("Dungeon: %d chambers averaging %dx%d cells against a target of %dx%d; entry at "
		     "X %d Z %d; %d cells eroded along the outline (%s), %d cells of unreachable pocket "
		     "filled. Ledges are spaced for a jump of %d cells — the character has to be able "
		     "to make it, nothing here can check that."),
		Chambers.Num(), TotalW / Chambers.Num(), TotalH / Chambers.Num(),
		TargetChamberCells.X, TargetChamberCells.Y,
		Entry.X, Entry.Y, Eroded,
		*FString::Printf(TEXT("floors %s, ceilings %s, walls %s"),
			bErodeFloors ? TEXT("on") : TEXT("off"),
			bErodeCeilings ? TEXT("on") : TEXT("off"),
			bErodeWalls ? TEXT("on") : TEXT("off")),
		Filled, FMath::Max(1, JumpHeightCells));
}

#undef LOCTEXT_NAMESPACE
