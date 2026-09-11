#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MazeGeneratorBase.generated.h"

struct FMazeGrid;

/**
 *  Data provider for the maze grid.
 *
 *  Manual drawing, random generation and image import are three implementations of
 *  one contract: they all fill the very same FMazeGrid, so the whole pipeline below
 *  (slicing, merge, export, streaming) is written once.
 *
 *  A new generator = a single derived class. It will show up in the panel's dropdown
 *  on its own, and its UPROPERTY fields in Details, without a single line of UI code.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class MAZEFORGECORE_API UMazeGeneratorBase : public UObject
{
	GENERATED_BODY()

public:
	/** Generation seed. The same value must always produce the same maze. */
	UPROPERTY(EditAnywhere, Category = "Generation")
	int32 Seed = 1337;

	/** Clear the grid before generating. Turn it off to draw on top of what is already there. */
	UPROPERTY(EditAnywhere, Category = "Generation")
	bool bClearBeforeGenerate = true;

	/** Entry point: sets up an FRandomStream from Seed and calls Generate. */
	void Execute(FMazeGrid& InOutGrid);

	/** Name for the dropdown and the logs. */
	virtual FText GetDisplayName() const;

protected:
	virtual void Generate(FMazeGrid& InOutGrid, FRandomStream& Rng)
		PURE_VIRTUAL(UMazeGeneratorBase::Generate, );
};
