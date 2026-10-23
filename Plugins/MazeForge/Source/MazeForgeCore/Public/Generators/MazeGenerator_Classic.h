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
	Branchy = 1,

	/**
	 *  The main path first, from the start to the exit, then a set number of branches off it, a
	 *  set depth each, then branches off those. What is not used stays solid unless Fill Leftover.
	 *  The main path and branches can be planned by hand with the Path tool.
	 */
	PathFirst = 2
};

/** A branch planned by hand: points in grid cells (X along the level, Y up). */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazePlannedBranch
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path Plan")
	TArray<FIntPoint> Points;
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

	// -------------------------------------------------------------- path first
	//
	// Style Path First only. Lengths are in corridor cells of the lattice, not grid cells.

	/** 0 — the main path goes the short way; 1 — it wanders as far as it can before the exit. */
	UPROPERTY(EditAnywhere, Category = "Path First", meta = (ClampMin = "0", ClampMax = "1",
		EditCondition = "Style == EMazeClassicStyle::PathFirst", EditConditionHides))
	float MainPathWinding = 0.5f;

	/** Branches off the main path, on top of any planned by hand. */
	UPROPERTY(EditAnywhere, Category = "Path First", meta = (ClampMin = "0", UIMax = "64",
		EditCondition = "Style == EMazeClassicStyle::PathFirst", EditConditionHides))
	int32 Branches = 6;

	UPROPERTY(EditAnywhere, Category = "Path First", meta = (ClampMin = "1", UIMax = "32",
		EditCondition = "Style == EMazeClassicStyle::PathFirst", EditConditionHides))
	int32 BranchDepthMin = 2;

	UPROPERTY(EditAnywhere, Category = "Path First", meta = (ClampMin = "1", UIMax = "64",
		EditCondition = "Style == EMazeClassicStyle::PathFirst", EditConditionHides))
	int32 BranchDepthMax = 6;

	/** Chance each cell of a branch starts a branch of its own, up to Max Nesting levels. */
	UPROPERTY(EditAnywhere, Category = "Path First", meta = (ClampMin = "0", ClampMax = "1",
		EditCondition = "Style == EMazeClassicStyle::PathFirst", EditConditionHides))
	float SubBranchChance = 0.15f;

	/** 1 — branches only off the main path; 2 — branches off those too; and so on. */
	UPROPERTY(EditAnywhere, Category = "Path First", meta = (ClampMin = "1", UIMax = "6",
		EditCondition = "Style == EMazeClassicStyle::PathFirst", EditConditionHides))
	int32 MaxNesting = 2;

	/**
	 *  Join every unused corridor cell on as short spurs off the branches. Off — they stay solid,
	 *  and the maze has exactly the branches asked for.
	 */
	UPROPERTY(EditAnywhere, Category = "Path First", meta = (
		EditCondition = "Style == EMazeClassicStyle::PathFirst", EditConditionHides))
	bool bFillLeftover = false;

	/** Optional: a library type (an Entry point) stood at the start of the main path. */
	UPROPERTY(EditAnywhere, Category = "Path First", meta = (
		EditCondition = "Style == EMazeClassicStyle::PathFirst", EditConditionHides))
	FName StartMarkerType;

	/** Optional: a library type (a Gate) stood at the end of the main path. */
	UPROPERTY(EditAnywhere, Category = "Path First", meta = (
		EditCondition = "Style == EMazeClassicStyle::PathFirst", EditConditionHides))
	FName ExitMarkerType;

	// ---------------------------------------------------------------- path plan
	//
	// Drawn with the Path tool in the viewport. The main path runs through its points in order,
	// the first being the start and the last the exit; each planned branch leaves the nearest
	// corridor already made and runs through its points. Empty — the main path is random.

	UPROPERTY(EditAnywhere, Category = "Path Plan", meta = (
		EditCondition = "Style == EMazeClassicStyle::PathFirst", EditConditionHides))
	TArray<FIntPoint> PlannedMainPath;

	UPROPERTY(EditAnywhere, Category = "Path Plan", meta = (
		EditCondition = "Style == EMazeClassicStyle::PathFirst", EditConditionHides))
	TArray<FMazePlannedBranch> PlannedBranches;


	UMazeGenerator_Classic();

	virtual FText GetDisplayName() const override;

protected:
	virtual void Generate(FMazeGrid& InOutGrid, FRandomStream& Rng) override;
};
