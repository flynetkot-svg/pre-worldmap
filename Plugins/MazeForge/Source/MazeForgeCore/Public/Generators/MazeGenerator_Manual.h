#pragma once

#include "CoreMinimal.h"
#include "Generators/MazeGeneratorBase.h"
#include "MazeGenerator_Manual.generated.h"

/**
 *  Manual drawing: the grid is filled by the designer with the brush in the editor,
 *  not by code.
 *
 *  The class is not formally empty — it marks the asset as hand-authored and protects
 *  the designer's work from an accidental "Run Generator" that would clear the grid.
 */
UCLASS(DisplayName = "Manual (drawn by hand)")
class MAZEFORGECORE_API UMazeGenerator_Manual : public UMazeGeneratorBase
{
	GENERATED_BODY()

public:
	UMazeGenerator_Manual();

	virtual FText GetDisplayName() const override;

protected:
	virtual void Generate(FMazeGrid& InOutGrid, FRandomStream& Rng) override;
};
