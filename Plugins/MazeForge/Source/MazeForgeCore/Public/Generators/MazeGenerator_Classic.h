#pragma once

#include "CoreMinimal.h"
#include "Generators/MazeGeneratorBase.h"
#include "MazeGenerator_Classic.generated.h"

UENUM()
enum class EMazeClassicStyle : uint8
{
	/** Recursive backtracker: long winding corridors and few, deep dead ends. The look of a puzzle-book maze. */
	LongCorridors = 0,

	/** Randomised Prim: many short branches and dead ends close to the main path. */
	Branchy = 1
};

/**
 *  The textbook maze — every point reachable, by exactly one path unless loops are asked for —
 *  laid out for a side view.
 *
 *  Drawn on a lattice of corridor cells. Seen from the side, a sideways link is a doorway in a
 *  wall, and an up-or-down link is a hole in the floor slab: the player drops through it, and
 *  climbs back up a ladder the generator stands in the hole. The grid holds mass only, so the
 *  ladder is a structural placement in the maze's spawn asset (see
 *  UMazeGeneratorBase::GetStructuralPlacements), of whichever library type Ladder Type names.
 *
 *  Every ladder spans the same height — Corridor Height + Floor Thickness — so one ladder type
 *  fits them all; the generator says the number in its log line.
 *
 *  The hole between rows r and r+1 sits at the left end of the cell when r is even and at the
 *  right end when it is odd. A cell linked both up and down therefore has its two holes at
 *  opposite ends, and the ladder from below never stands on top of the hole going further down.
 *
 *  Size, depth and borders come from FMazeGrid, as with every generator. The lattice fills
 *  the working area from the bottom-left; what does not fit a whole cell stays solid.
 */
UCLASS(DisplayName = "Classic (perfect maze)")
class MAZEFORGECORE_API UMazeGenerator_Classic : public UMazeGeneratorBase
{
	GENERATED_BODY()

public:
	// --------------------------------------------------------------------- style

	UPROPERTY(EditAnywhere, Category = "Style")
	EMazeClassicStyle Style = EMazeClassicStyle::LongCorridors;

	/**
	 *  How likely an up-or-down link is next to a sideways one. 1 — no preference; lower —
	 *  longer horizontal runs and fewer ladders, which is what a side-scroller reads best.
	 *  Never zero: every row still gets connected, only later and more rarely.
	 */
	UPROPERTY(EditAnywhere, Category = "Style", meta = (ClampMin = "0.05", ClampMax = "1"))
	float VerticalBias = 0.35f;

	/**
	 *  Share of the remaining walls knocked through after the maze is done. 0 — a perfect maze,
	 *  one path between any two points; a little — shortcuts, so a wrong turn is not always a
	 *  walk back. Vertical walls and floors are weighed by Vertical Bias here too.
	 */
	UPROPERTY(EditAnywhere, Category = "Style", meta = (ClampMin = "0", ClampMax = "1"))
	float LoopChance = 0.0f;

	// --------------------------------------------------------------------- cells

	/** Length of one corridor cell along X, in grid cells. Two ladder holes must fit in it. */
	UPROPERTY(EditAnywhere, Category = "Cells", meta = (ClampMin = "2", UIMax = "128"))
	int32 CorridorWidth = 6;

	/** Clear height of a corridor. More than the character: the stock capsule is two cells. */
	UPROPERTY(EditAnywhere, Category = "Cells", meta = (ClampMin = "3", UIMax = "128"))
	int32 CorridorHeight = 4;

	/** Thickness of the walls between corridor cells. */
	UPROPERTY(EditAnywhere, Category = "Cells", meta = (ClampMin = "1", UIMax = "32"))
	int32 WallThickness = 1;

	/** Thickness of the floor slabs between rows. */
	UPROPERTY(EditAnywhere, Category = "Cells", meta = (ClampMin = "1", UIMax = "32"))
	int32 FloorThickness = 1;

	// ------------------------------------------------------------------- ladders

	/**
	 *  The library type stood in every hole. Category Climb, Anchor Floor, and a footprint as
	 *  tall as Corridor Height + Floor Thickness — the generator logs the number. Empty — no
	 *  ladders: the holes can be fallen through and never climbed.
	 */
	UPROPERTY(EditAnywhere, Category = "Ladders")
	FName LadderType;

	/** Width of the hole in a floor slab. Two at least: a one-cell hole catches the shoulders. */
	UPROPERTY(EditAnywhere, Category = "Ladders", meta = (ClampMin = "2", UIMax = "32"))
	int32 HoleWidth = 2;


	UMazeGenerator_Classic();

	virtual FText GetDisplayName() const override;

protected:
	virtual void Generate(FMazeGrid& InOutGrid, FRandomStream& Rng) override;
};
