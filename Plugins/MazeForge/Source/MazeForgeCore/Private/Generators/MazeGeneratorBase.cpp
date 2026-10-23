#include "Generators/MazeGeneratorBase.h"

#include "Data/MazeGrid.h"
#include "Generators/MazeLayout2D.h"
#include "MazeForgeCore.h"

#define LOCTEXT_NAMESPACE "MazeForge"

UMazeGeneratorBase::UMazeGeneratorBase()
{
	// What a mix looks like when the chance is turned up: no single shape everywhere.
	DeadEndShapes = {
		FMazeDeadEndShapeChoice(EMazeDeadEndShape::Arrow, 1.0f),
		FMazeDeadEndShapeChoice(EMazeDeadEndShape::SlantDown, 1.0f),
		FMazeDeadEndShapeChoice(EMazeDeadEndShape::Round, 1.0f),
		FMazeDeadEndShapeChoice(EMazeDeadEndShape::Flat, 1.0f) };
}

void UMazeGeneratorBase::Execute(FMazeGrid& InOutGrid)
{
	if (bClearBeforeGenerate)
	{
		InOutGrid.Reset();
	}

	FRandomStream Rng(Seed);
	StructuralPlacements.Reset();
	ProtectedRects.Reset();
	bOutlineShaped = false;
	LastDeadEnds = 0;
	LastEroded = 0;

	const double StartTime = FPlatformTime::Seconds();
	Generate(InOutGrid, Rng);
	const double Elapsed = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogMazeForge, Log,
		TEXT("%s: generated %d cells in %.3f s (seed %d); %d dead end(s) shaped, %d cell(s) eroded"),
		*GetDisplayName().ToString(), InOutGrid.NumCells(), Elapsed, Seed, LastDeadEnds, LastEroded);
}

void UMazeGeneratorBase::AddStructuralPlacement(FMazePlacement Placement)
{
	Placement.bStructural = true;
	Placement.bGenerated = false;
	Placement.Id = 0;
	StructuralPlacements.Add(MoveTemp(Placement));
}

void UMazeGeneratorBase::ProtectRect(const FIntPoint& Min, const FIntPoint& Max)
{
	ProtectedRects.Emplace(Min, Max);
}

int32 UMazeGeneratorBase::ShapeOutline(FMazeLayout2D& Layout, const FIntPoint& Min, const FIntPoint& Max,
                                       FRandomStream& Rng)
{
	bOutlineShaped = true;

	TBitArray<> Protected(false, Layout.Width() * Layout.Height());
	for (const TPair<FIntPoint, FIntPoint>& Rect : ProtectedRects)
	{
		for (int32 Z = FMath::Max(0, Rect.Key.Y); Z < FMath::Min(Layout.Height(), Rect.Value.Y); ++Z)
		{
			for (int32 X = FMath::Max(0, Rect.Key.X); X < FMath::Min(Layout.Width(), Rect.Value.X); ++X)
			{
				Protected[Layout.Index(X, Z)] = true;
			}
		}
	}

	FMazeDeadEndParams DeadEnds;
	DeadEnds.Chance = DeadEndChance;
	DeadEnds.Shapes = &DeadEndShapes;
	DeadEnds.Length = DeadEndLength;
	DeadEnds.MinHeight = FMath::Max(2, AgentHeightCells);
	DeadEnds.MaxHeight = DeadEndMaxHeight > 0 ? DeadEndMaxHeight : MAX_int32;
	DeadEnds.Min = Min;
	DeadEnds.Max = Max;
	DeadEnds.Protected = &Protected;
	LastDeadEnds = MazeShapeDeadEnds(Layout, DeadEnds, Rng);

	FMazeErosionParams Erosion;
	Erosion.Amount = ErosionAmount;
	Erosion.Passes = ErosionPasses;
	Erosion.bFloors = bErodeFloors;
	Erosion.bCeilings = bErodeCeilings;
	Erosion.bWalls = bErodeWalls;
	Erosion.KeepWallCells = ErosionKeepWallCells;
	Erosion.AgentHeightCells = AgentHeightCells;
	Erosion.Min = Min;
	Erosion.Max = Max;
	Erosion.Protected = ProtectedRects.Num() > 0 ? &Protected : nullptr;
	LastEroded = MazeErodeOutline(Layout, Erosion, Rng);

	// No loose crumbs of mass, no pinholes: erosion's leftovers, never the drawing's own.
	if (LastEroded > 0)
	{
		// Crumbs: what erosion sticks on is single cells, a designed ledge is longer. Holes: any
		// closed pocket the player could not stand in anyway.
		const int32 MaxHole = FMath::Max(2, AgentHeightCells);
		LastEroded += MazeCleanOutline(Layout, Min, Max, 2, MaxHole, ProtectedRects.Num() > 0 ? &Protected : nullptr);
	}

	return LastEroded;
}

void UMazeGeneratorBase::WriteLayout(FMazeLayout2D& Layout, FMazeGrid& InOutGrid, FRandomStream& Rng)
{
	if (!bOutlineShaped)
	{
		// Inside the closed world borders, as the generators themselves draw: the grid paints
		// the borders on its own, and shaping into one would open the world's edge.
		const int32 Border = InOutGrid.Borders.ThicknessCells;
		const FIntPoint Min(InOutGrid.Borders.bCloseLeft ? Border : 0,
		                    InOutGrid.Borders.bCloseBottom ? Border : 0);
		const FIntPoint Max(Layout.Width() - (InOutGrid.Borders.bCloseRight ? Border : 0),
		                    Layout.Height() - (InOutGrid.Borders.bCloseTop ? Border : 0));
		ShapeOutline(Layout, Min, Max, Rng);
	}

	MazeWriteLayoutToGrid(Layout, InOutGrid);
}

FText UMazeGeneratorBase::GetDisplayName() const
{
	return LOCTEXT("GeneratorBase", "Generator");
}

#undef LOCTEXT_NAMESPACE
