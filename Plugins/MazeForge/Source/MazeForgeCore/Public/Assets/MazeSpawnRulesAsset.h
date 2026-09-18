#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/MazeTypes.h"
#include "MazeSpawnRulesAsset.generated.h"

/**
 *  One kind of object, and how much of it a room should get.
 *
 *  Counts per room rather than a share of the space, and that is a choice about what a designer
 *  can hold in their head. "Two to five crates in a room" is a sentence you can picture; "five
 *  per cent of the cells this fits in" is a number whose result you find out by pressing the
 *  button. The price is that a room twice the size gets the same handful — which is the right
 *  trade while rooms are uniform, and the thing to revisit when they stop being.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeSpawnRule
{
	GENERATED_BODY()

	/** Which type in the object library. Must match a TypeId there exactly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	FName TypeId;

	/** Switched off without deleting, for trying a pass with one thing removed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	bool bEnabled = true;

	/** Fewest per room. A room with nowhere to put them gets fewer and says so. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule", meta = (ClampMin = "0"))
	int32 MinPerRoom = 1;

	/** Most per room. Below the minimum it is treated as the minimum. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule", meta = (ClampMin = "0"))
	int32 MaxPerRoom = 3;

	/**
	 *  How many empty cells to keep between this object and anything already placed.
	 *
	 *  Not a correctness rule — zero spacing still refuses to overlap. It is what stops a room
	 *  from looking poured rather than furnished: three crates touching each other read as one
	 *  lump, and a generator with no spacing finds exactly those spots first.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule", meta = (ClampMin = "0"))
	int32 MinSpacingCells = 2;

	/** Which depth band the objects go in. Play is where the player can touch them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	EMazeDepthBand Band = EMazeDepthBand::Play;
};

/**
 *  How a maze gets furnished.
 *
 *  Its own asset rather than fields on the grid or on the object library, and the reason is
 *  reuse in both directions. The library says what a crate IS — its mesh, its footprint, where
 *  it can stand — and that is the same in every maze. The grid is one maze. How densely crates
 *  are scattered is neither: it is a decision about the game, and ten mazes should be able to
 *  share one answer and change it in one place. The same argument that gave the streaming its
 *  own rules asset instead of numbers on the character.
 */
UCLASS(BlueprintType)
class MAZEFORGECORE_API UMazeSpawnRulesAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn Rules")
	TArray<FMazeSpawnRule> Rules;

	/**
	 *  What makes a run repeatable.
	 *
	 *  Two runs over the same maze with the same seed produce the same furniture, so a layout
	 *  can be judged, adjusted and judged again. Change it to get a different arrangement of
	 *  the same rules.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn Rules")
	int32 Seed = 1337;

	/** Rules that name a type, in order. Disabled and unnamed ones are skipped. */
	void GetActiveRules(TArray<FMazeSpawnRule>& OutRules) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
