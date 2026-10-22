#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/MazeSpawnTypes.h"
#include "Generators/MazeOutlineShaping.h"
#include "MazeGeneratorBase.generated.h"

struct FMazeGrid;
struct FMazeLayout2D;

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

	// ------------------------------------------------------------------ movement

	/**
	 *  The player's height in cells. A passage lower than this is a wall to him.
	 *
	 *  Every generator measures reachability with it, and the shaping below keeps this much
	 *  headroom clear above every floor. The stock UE capsule is 176 cm: two cells of 100,
	 *  four of 50.
	 */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "1", UIMax = "64"))
	int32 AgentHeightCells = 2;

	// ---------------------------------------------------------- shaping the outline
	//
	// Shared by every generator and run on its finished drawing, dead ends first, erosion
	// after (so the ends get worn as well). Everything off by default: a maze that was
	// generated before these existed comes out the same for its seed.

	/**
	 *  Share of passage ends that get a shape instead of a square wall.
	 *
	 *  A passage end: a straight stretch of corridor no taller than Dead End Max Height, closed
	 *  by a wall with nothing leading on — no door, no hole, no ladder. Found in the drawing
	 *  itself, so it works the same for every generator, hand-drawn images included.
	 */
	UPROPERTY(EditAnywhere, Category = "Dead Ends", meta = (ClampMin = "0", ClampMax = "1"))
	float DeadEndChance = 0.0f;

	/** The shapes to pick from, by weight. Flat in the mix keeps a share of the ends square. */
	UPROPERTY(EditAnywhere, Category = "Dead Ends", meta = (TitleProperty = "Shape"))
	TArray<FMazeDeadEndShapeChoice> DeadEndShapes;

	/**
	 *  How far back from the end wall the shape reaches, in cells. 0 — at about 45 degrees:
	 *  the passage height for a slant, half of it for an arrow or a half-circle, a quarter for
	 *  a polygon. Always leaves at least one full-height column of the passage open.
	 */
	UPROPERTY(EditAnywhere, Category = "Dead Ends", meta = (ClampMin = "0", UIMax = "64"))
	int32 DeadEndLength = 0;

	/**
	 *  Passages taller than this are halls, and their ends are left alone. 0 — no limit: every
	 *  passage end counts, however tall. The slider stops at 64; a larger number can be typed.
	 */
	UPROPERTY(EditAnywhere, Category = "Dead Ends", meta = (ClampMin = "0", UIMax = "64"))
	int32 DeadEndMaxHeight = 0;

	/**
	 *  Share of the outline changed per pass. 0 — ruler-straight.
	 *
	 *  Bites cells out of the mass and sticks cells back on to it. A wall or floor one cell
	 *  thick cannot be bitten — that would go straight through — so only outcrops grow on it;
	 *  give the mass two or three cells for real notches.
	 */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = "0", ClampMax = "1"))
	float ErosionAmount = 0.0f;

	/** More passes let a bite deepen into a notch and a bump grow into an outcrop. */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = "1", ClampMax = "6"))
	int32 ErosionPasses = 2;

	/**
	 *  Rough up the floors the player runs along. Off by default: a notched floor is a floor
	 *  with steps in it, and the character catches on every one.
	 */
	UPROPERTY(EditAnywhere, Category = "Erosion")
	bool bErodeFloors = false;

	/** Rough up the undersides of slabs. Nobody walks there — pure texture. */
	UPROPERTY(EditAnywhere, Category = "Erosion")
	bool bErodeCeilings = true;

	/** Rough up the vertical faces. Also pure texture. */
	UPROPERTY(EditAnywhere, Category = "Erosion")
	bool bErodeWalls = true;

	/** Never bite into mass thinner than this: a wall eaten through is a passage nobody planned. */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = "1", UIMax = "32"))
	int32 ErosionKeepWallCells = 2;

	UMazeGeneratorBase();

	/** Entry point: sets up an FRandomStream from Seed and calls Generate. */
	void Execute(FMazeGrid& InOutGrid);

	/** Name for the dropdown and the logs. */
	virtual FText GetDisplayName() const;

	/**
	 *  Objects the last run decided the maze cannot be walked without — the ladders in its
	 *  shafts. Empty for most generators.
	 *
	 *  The grid holds mass and nothing else, so a generator that needs a ladder cannot draw one;
	 *  it says where, and UMazeGridAsset::RunGenerator puts it into the maze's spawn asset as a
	 *  structural placement. Output of one run, not a setting: never saved.
	 */
	const TArray<FMazePlacement>& GetStructuralPlacements() const { return StructuralPlacements; }

protected:
	virtual void Generate(FMazeGrid& InOutGrid, FRandomStream& Rng)
		PURE_VIRTUAL(UMazeGeneratorBase::Generate, );

	/** For Generate: an object that belongs to the maze itself. Marked structural here. */
	void AddStructuralPlacement(FMazePlacement Placement);

	/**
	 *  How every generator puts its drawing into the grid: shapes the outline (dead ends, then
	 *  erosion) inside the area the world borders leave, then writes. Skips the shaping when
	 *  the generator already ran ShapeOutline itself.
	 */
	void WriteLayout(FMazeLayout2D& Layout, FMazeGrid& InOutGrid, FRandomStream& Rng);

	/**
	 *  Dead ends, then erosion, inside Min..Max (Max exclusive), right now. For a generator whose
	 *  later steps must see the shaped outline — Dungeon fills unreachable pockets after it.
	 *  Returns how many cells erosion changed.
	 */
	int32 ShapeOutline(FMazeLayout2D& Layout, const FIntPoint& Min, const FIntPoint& Max, FRandomStream& Rng);

	/** Cells the shaping must leave alone — a ladder's shaft and its landing. Max exclusive. */
	void ProtectRect(const FIntPoint& Min, const FIntPoint& Max);

private:
	TArray<FMazePlacement> StructuralPlacements;

	/** Output of one run, like the placements above. */
	TArray<TPair<FIntPoint, FIntPoint>> ProtectedRects;
	bool bOutlineShaped = false;
	int32 LastDeadEnds = 0;
	int32 LastEroded = 0;
};
