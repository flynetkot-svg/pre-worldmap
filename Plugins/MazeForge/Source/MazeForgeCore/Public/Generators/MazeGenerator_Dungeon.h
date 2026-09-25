#pragma once

#include "CoreMinimal.h"
#include "Generators/MazeGeneratorBase.h"
#include "MazeGenerator_Dungeon.generated.h"

struct FMazeLayout2D;

/**
 *  Chambers with thick walls, joined by doors and chimneys.
 *
 *  The other shape a 2.5D level can take. Where the Saboteur generator builds a building —
 *  floors that run the width of the map and shafts that cut through all of them — this one
 *  builds a dungeon: rooms that are closed on every side, and you leave one through a
 *  doorway that somebody cut.
 *
 *  The difference that matters is not the look, it is how you get up. In a building you
 *  climb a shaft; in a dungeon you jump from ledge to ledge. So the parameter this whole
 *  generator is written around is JumpHeightCells, and every ledge it places is within a
 *  jump of the one below it. Nothing else can check that for us: the flood fill treats air
 *  as traversable — it has to, because the player falls as well as walks — so a ledge ten
 *  cells above the floor is perfectly "reachable" to it and perfectly unreachable to a
 *  player. Climbability is guaranteed by construction here or not at all.
 *
 *  It places no ladders. A ladder is an object, not geometry: it lives in the object
 *  library under the Climb category and is put down by the brush or the spawner. A
 *  generator that carved ladder-shaped holes would be deciding something that is not its
 *  to decide.
 *
 *  The world size, the depth and the borders are not duplicated here — they live in
 *  FMazeGrid. Two sources of truth for one value drift apart sooner or later.
 */
UCLASS(DisplayName = "Dungeon (chambers and doors)")
class MAZEFORGECORE_API UMazeGenerator_Dungeon : public UMazeGeneratorBase
{
	GENERATED_BODY()

public:
	// ----------------------------------------------------------------- chambers

	/**
	 *  Roughly how big a chamber should be, in cells: X along the level, Y up.
	 *
	 *  Say the room you want and the split runs until the sectors are near it, rather than
	 *  the other way round. The first version of this took a minimum side and a split depth,
	 *  which is the same information turned inside out: you set "how many times to halve the
	 *  map" and found out the room size afterwards, by looking.
	 *
	 *  Wide and low is the default, and it is not a stylistic preference. A side-scroller is
	 *  walked along; height is expensive, because every cell of it has to be climbed. A
	 *  square room reads as a pit. The original this is modelled on runs floors of fifty
	 *  cells with six cells of headroom.
	 */
	UPROPERTY(EditAnywhere, Category = "Chambers", meta = (ClampMin = "6", UIMax = "120"))
	FIntPoint TargetChamberCells = FIntPoint(44, 14);

	/**
	 *  How far a chamber may stray from the target, as a fraction.
	 *
	 *  Zero gives a grid of identical boxes, which reads as a spreadsheet. Around a third is
	 *  enough for the rooms to differ without any of them becoming a corridor.
	 */
	UPROPERTY(EditAnywhere, Category = "Chambers", meta = (ClampMin = "0", ClampMax = "0.9"))
	float ChamberVariance = 0.35f;

	/**
	 *  A stop on the recursion, not a way to control the size.
	 *
	 *  The target above is what decides how big a chamber comes out. This is here so that a
	 *  target of two cells on a map of eight hundred cannot ask for a hundred thousand
	 *  chambers and take the editor with it.
	 */
	UPROPERTY(EditAnywhere, Category = "Chambers", meta = (ClampMin = "1", ClampMax = "12"))
	int32 MaxSplitDepth = 9;

	/**
	 *  Thickness of the wall around a chamber, in cells.
	 *
	 *  Two chambers side by side are separated by twice this. It is what makes a dungeon
	 *  read as rooms rather than as one hall with pillars: a thin wall with a hole in it
	 *  looks like a gap, a thick wall with a hole in it looks like a doorway.
	 */
	UPROPERTY(EditAnywhere, Category = "Chambers", meta = (ClampMin = "1", UIMax = "8"))
	int32 WallThickness = 3;

	// -------------------------------------------------------------------- ledges

	/**
	 *  Fraction of the ledge slots that actually get a ledge.
	 *
	 *  One is a full floor at every level and a chamber you can cross anywhere; zero is an
	 *  empty box. Between them is a room worth walking through.
	 */
	UPROPERTY(EditAnywhere, Category = "Ledges", meta = (ClampMin = "0", ClampMax = "1"))
	float LedgeChance = 0.7f;

	UPROPERTY(EditAnywhere, Category = "Ledges", meta = (ClampMin = "2", UIMax = "40"))
	int32 LedgeMinLength = 4;

	UPROPERTY(EditAnywhere, Category = "Ledges", meta = (ClampMin = "2", UIMax = "60"))
	int32 LedgeMaxLength = 12;

	UPROPERTY(EditAnywhere, Category = "Ledges", meta = (ClampMin = "1", UIMax = "4"))
	int32 LedgeThickness = 1;

	// ------------------------------------------------------------------ movement

	/**
	 *  The player's height in cells. A passage lower than this is a wall.
	 *
	 *  The stock UE capsule is 176 cm, which is two cells of 100.
	 */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "1", UIMax = "6"))
	int32 AgentHeightCells = 2;

	/**
	 *  How far up the player can get from a standing start, in cells.
	 *
	 *  The number this generator is built around. Ledges are spaced by it, doorways are cut
	 *  at heights reachable by it, and the steps inside a chimney are placed by it. Set it
	 *  larger than the character can actually manage and the dungeon becomes a set of
	 *  beautiful rooms with no way out of them — and nothing will report that, because to
	 *  every check in the plugin the air between two ledges is free space.
	 */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "1", UIMax = "6"))
	int32 JumpHeightCells = 2;

	// --------------------------------------------------------------------- doors

	/**
	 *  Height of a doorway, in cells. Below the agent's height it is raised to it.
	 *
	 *  A door the player cannot walk through is not a door, and the whole point of this
	 *  generator is that the rooms are connected.
	 */
	UPROPERTY(EditAnywhere, Category = "Doors", meta = (ClampMin = "1", UIMax = "8"))
	int32 DoorHeightCells = 3;

	/** Width of the vertical chimney between chambers stacked one above the other. */
	UPROPERTY(EditAnywhere, Category = "Doors", meta = (ClampMin = "2", UIMax = "8"))
	int32 ChimneyWidthCells = 3;

	// ------------------------------------------------------------------- erosion

	/**
	 *  How much of the outline to chew away, as a fraction of the cells along it.
	 *
	 *  What separates a dungeon from a spreadsheet. Everything above draws rectangles, and a
	 *  room that is exactly a rectangle reads as a room somebody typed in. One pass over the
	 *  boundary that bites cells out of the mass and sticks cells back on to it costs almost
	 *  nothing and is most of the difference.
	 *
	 *  Zero leaves the outlines straight, which is the right answer for a clean industrial
	 *  interior and the wrong one for rock.
	 */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = "0", ClampMax = "1"))
	float ErosionAmount = 0.35f;

	/**
	 *  How many times to pass over the outline.
	 *
	 *  One pass only ever moves the boundary by a cell, so the edge comes out evenly rough.
	 *  Two or three let a bite deepen into a notch and a bump grow into an outcrop, because
	 *  each pass works on what the last one left — which is what makes the result look worn
	 *  rather than dithered.
	 */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = "1", ClampMax = "6"))
	int32 ErosionPasses = 2;

	/**
	 *  Rough up the surfaces the player runs along.
	 *
	 *  Off by default, and it is the one of the three that changes how the level plays rather
	 *  than how it looks. A bitten floor is a floor with steps in it: the character catches on
	 *  every notch, a run that should be one movement becomes a series of little climbs, and
	 *  the fault reads as bad controls rather than as bad ground. A bump added on top of a
	 *  floor does the same thing the other way up.
	 *
	 *  Both are covered here: the flag protects the walkable surface, not just the mass under
	 *  it, so neither biting into the top of a floor nor growing a lump on it happens while it
	 *  is off.
	 */
	UPROPERTY(EditAnywhere, Category = "Erosion")
	bool bErodeFloors = false;

	/** Rough up the undersides of slabs. Nobody walks there — it is pure texture. */
	UPROPERTY(EditAnywhere, Category = "Erosion")
	bool bErodeCeilings = true;

	/** Rough up the vertical faces to the left and right. Also pure texture. */
	UPROPERTY(EditAnywhere, Category = "Erosion")
	bool bErodeWalls = true;

	/**
	 *  Never bite into a wall thinner than this, in cells.
	 *
	 *  The guard that keeps erosion decorative. A wall eaten through is not a rough edge, it
	 *  is a hole between two chambers that no door accounts for — and since the chambers are
	 *  joined by construction and checked before this runs, nothing downstream would notice.
	 */
	UPROPERTY(EditAnywhere, Category = "Erosion", meta = (ClampMin = "1", UIMax = "8"))
	int32 ErosionKeepWallCells = 2;

	// -------------------------------------------------------------- connectivity

	/**
	 *  Fill in pockets the player cannot reach instead of leaving them.
	 *
	 *  The chambers are joined by construction, so what this catches is the leftovers: a gap
	 *  behind a ledge, a corner a doorway did not open. Small ones are filled, and that is
	 *  all the flood fill is asked to do here — it cannot tell a climbable room from an
	 *  unclimbable one, and pretending otherwise is how a generator earns trust it has not got.
	 */
	UPROPERTY(EditAnywhere, Category = "Connectivity")
	bool bFillUnreachablePockets = true;

	/** Pockets larger than this are left alone: at some size it is a room, not a mistake. */
	UPROPERTY(EditAnywhere, Category = "Connectivity", meta = (ClampMin = "1", UIMax = "400"))
	int32 MaxPocketCells = 40;

	virtual FText GetDisplayName() const override;

protected:
	virtual void Generate(FMazeGrid& InOutGrid, FRandomStream& Rng) override;

private:
	/** A rectangle in XZ. Min inclusive, Max exclusive. */
	struct FRect
	{
		FIntPoint Min = FIntPoint::ZeroValue;
		FIntPoint Max = FIntPoint::ZeroValue;

		int32 Width() const { return Max.X - Min.X; }
		int32 Height() const { return Max.Y - Min.Y; }

		FIntPoint Centre() const
		{
			return FIntPoint((Min.X + Max.X) / 2, (Min.Y + Max.Y) / 2);
		}
	};

	/**
	 *  One chamber and the way it was cut.
	 *
	 *  Inner is the empty space; Floor is the Z of the first cell a player can stand on.
	 *  Both are kept because the doors need them and recomputing them from the layout after
	 *  the ledges are in would give a different answer.
	 */
	struct FChamber
	{
		FRect Inner;
		int32 FloorZ = 0;
	};

	/**
	 *  Splits the area and carves a chamber in every leaf, joining the two halves of every
	 *  node as it comes back up. Returns the index of a chamber that stands for this
	 *  subtree, or INDEX_NONE if nothing was carved in it.
	 *
	 *  The joining happens here, on the way out of the recursion, and that is the whole
	 *  reason the split is not a flat loop: two halves of one node are guaranteed to be
	 *  adjacent and guaranteed to be on the correct side of each other. Pairing chambers up
	 *  afterwards by comparing rectangles would have to rediscover both facts, and get one
	 *  of them wrong on the day the split stops being a clean BSP.
	 */
	int32 SplitAndCarve(FMazeLayout2D& Layout, const FRect& Area, int32 Depth,
	                    FRandomStream& Rng, TArray<FChamber>& InOutChambers) const;

	void CarveChamber(FMazeLayout2D& Layout, const FRect& Area,
	                  FRandomStream& Rng, TArray<FChamber>& InOutChambers) const;

	/** Ledges inside one chamber, each within a jump of the one below. */
	void CarveLedges(FMazeLayout2D& Layout, const FChamber& Chamber, FRandomStream& Rng) const;

	/** A doorway through the wall between two chambers standing side by side. */
	void CarveDoor(FMazeLayout2D& Layout, const FChamber& Left, const FChamber& Right,
	               FRandomStream& Rng) const;

	/** A climbable shaft between two chambers stacked one above the other. */
	void CarveChimney(FMazeLayout2D& Layout, const FChamber& Lower, const FChamber& Upper,
	                  FRandomStream& Rng) const;

	/**
	 *  Roughens the boundary between mass and air. Returns how many cells changed.
	 *
	 *  Runs on the finished layout, after the doors and the chimneys, and deliberately so: it
	 *  reads the outline as it actually ended up rather than the one the rectangles implied.
	 *  It refuses to touch anything that would thin a wall past ErosionKeepWallCells, thin a
	 *  floor a player stands on, or lower a ceiling onto his head.
	 */
	int32 ErodeOutline(FMazeLayout2D& Layout, const FRect& Area, FRandomStream& Rng) const;

	/** Where the player starts: the floor of the lowest chamber. */
	FIntPoint FindEntryPoint(const TArray<FChamber>& Chambers) const;

	/** Returns how many cells were filled in. */
	int32 FillPockets(FMazeLayout2D& Layout, const FIntPoint& Entry) const;
};
