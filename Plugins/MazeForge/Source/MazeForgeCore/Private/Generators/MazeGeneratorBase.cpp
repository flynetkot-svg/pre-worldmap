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

	const double StartTime = FPlatformTime::Seconds();
	Generate(InOutGrid, Rng);
	const double Elapsed = FPlatformTime::Seconds() - StartTime;

	UE_LOG(LogMazeForge, Log, TEXT("%s: generated %d cells in %.3f s (seed %d)"),
		*GetDisplayName().ToString(), InOutGrid.NumCells(), Elapsed, Seed);
}

FText UMazeGeneratorBase::GetDisplayName() const
{
	return LOCTEXT("GeneratorBase", "Generator");
}

#undef LOCTEXT_NAMESPACE
