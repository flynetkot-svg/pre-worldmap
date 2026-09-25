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

	/**
	 *  Paint the back wall over the whole map once the drawing is done.
	 *
	 *  The same work as the Fill Back Wall button, saved from being forgotten. Without a back
	 *  wall every corridor is a hole straight through the depth, and the camera looks through
	 *  it past the maze at the skybox — which is invisible in the editor and obvious in the
	 *  game, usually to somebody else.
	 *
	 *  On the base class, so every generator has it: the question "is there anything behind
	 *  what I just drew" belongs to none of them in particular.
	 *
	 *  Off by default, because it is not free — a wall behind every empty column is real
	 *  geometry — and because a maze meant to be seen against the sky is a legitimate thing
	 *  to draw.
	 */
	UPROPERTY(EditAnywhere, Category = "Generation")
	bool bFillBackWallAfterGenerate = false;

	/**
	 *  Which surface variant the back wall is painted with.
	 *
	 *  Which variants exist is decided by the palette in the build settings, per cell type.
	 *  An index the type does not declare is not an error — the wall simply gets the base
	 *  surface.
	 */
	UPROPERTY(EditAnywhere, Category = "Generation",
		meta = (ClampMin = "0", ClampMax = "63", EditCondition = "bFillBackWallAfterGenerate"))
	int32 BackWallVariant = 0;

	/** Entry point: sets up an FRandomStream from Seed and calls Generate. */
	void Execute(FMazeGrid& InOutGrid);

	/** Name for the dropdown and the logs. */
	virtual FText GetDisplayName() const;

protected:
	virtual void Generate(FMazeGrid& InOutGrid, FRandomStream& Rng)
		PURE_VIRTUAL(UMazeGeneratorBase::Generate, );
};
