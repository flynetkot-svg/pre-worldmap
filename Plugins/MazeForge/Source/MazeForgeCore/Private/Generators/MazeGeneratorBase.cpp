#include "Generators/MazeGeneratorBase.h"

#include "Data/MazeGrid.h"
#include "MazeForgeCore.h"

#define LOCTEXT_NAMESPACE "MazeForge"

void UMazeGeneratorBase::Execute(FMazeGrid& InOutGrid)
{
	if (bClearBeforeGenerate)
	{
		InOutGrid.Reset();
	}

	FRandomStream Rng(Seed);
	StructuralPlacements.Reset();

	const double StartTime = FPlatformTime::Seconds();
	Generate(InOutGrid, Rng);
	const double Elapsed = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogMazeForge, Log, TEXT("%s: generated %d cells in %.3f s (seed %d)"),
		*GetDisplayName().ToString(), InOutGrid.NumCells(), Elapsed, Seed);
}

void UMazeGeneratorBase::AddStructuralPlacement(FMazePlacement Placement)
{
	Placement.bStructural = true;
	Placement.bGenerated = false;
	Placement.Id = 0;
	StructuralPlacements.Add(MoveTemp(Placement));
}

FText UMazeGeneratorBase::GetDisplayName() const
{
	return LOCTEXT("GeneratorBase", "Generator");
}

#undef LOCTEXT_NAMESPACE
