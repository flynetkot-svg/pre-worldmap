#include "Generators/MazeGenerator_Manual.h"

#include "Data/MazeGrid.h"
#include "MazeForgeCore.h"

#define LOCTEXT_NAMESPACE "MazeForge"

UMazeGenerator_Manual::UMazeGenerator_Manual()
{
	// A hand-drawn grid must not be cleared: it is the designer's work.
	bClearBeforeGenerate = false;
}

FText UMazeGenerator_Manual::GetDisplayName() const
{
	return LOCTEXT("GeneratorManual", "Manual");
}

void UMazeGenerator_Manual::Generate(FMazeGrid& InOutGrid, FRandomStream& Rng)
{
	UE_LOG(LogMazeForge, Log,
		TEXT("Manual: the grid is drawn by hand, nothing to generate (%d cells kept)."),
		InOutGrid.NumCells());
}

#undef LOCTEXT_NAMESPACE
