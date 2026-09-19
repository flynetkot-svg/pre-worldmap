#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/MazeTypes.h"
#include "Data/MazeSpawnTypes.h"
#include "MazeSpawnRulesAsset.generated.h"

/** Whether a rule names one object or a whole category of them. */
UENUM(BlueprintType)
enum class EMazeRuleTarget : uint8
{
	/** One type from the library, by id. */
	Type     = 0,

	/**
	 *  Any type of this category, drawn per object.
	 *
	 *  The draw is per object rather than per rule on purpose: "three decor in this room"
	 *  should be a crate, a barrel and a wardrobe. Drawn once per rule it would be three
	 *  wardrobes, which is the arrangement a designer would have got by writing one rule and
	 *  is therefore the one worth nothing.
	 */
	Category = 1
};

/** Which rooms a rule cares about, by what the maze already says about them. */
UENUM(BlueprintType)
enum class EMazeRoomTransitions : uint8
{
	/** The filter ignores transitions. */
	Any   = 0,
	/** Only rooms holding a Gate or an Entry. */
	Only  = 1,
	/** Only rooms holding neither. */
	Never = 2
};

/**
 *  Which rooms a rule applies to.
 *
 *  Reading properties the maze already has rather than tags put on rooms by hand, and that is
 *  the whole design decision. Rooms are produced by the slicer: change Room Size and every room
 *  is a new room with a new id, so anything typed onto a room by hand dies the next time the
 *  maze is re-sliced — quietly, because the tag would simply be gone and the rule would go on
 *  matching nothing. The number of exits and whether a transition stands in the room survive
 *  re-slicing because they are recomputed from the maze itself.
 *
 *  It buys the sentences worth saying: enemies only where there is more than one way out,
 *  supplies in dead ends, nothing loose in the room you arrive into.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeRoomFilter
{
	GENERATED_BODY()

	/** Fewest exits the room must have. 0 means no limit. A dead end has one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rooms", meta = (ClampMin = "0"))
	int32 MinExits = 0;

	/** Most exits the room may have. 0 means no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rooms", meta = (ClampMin = "0"))
	int32 MaxExits = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rooms")
	EMazeRoomTransitions Transitions = EMazeRoomTransitions::Any;

	/** True when a room with this many exits and this answer about transitions qualifies. */
	bool Matches(int32 Exits, bool bHasTransition) const;

	/** True when the filter lets every room through — for the report line. */
	bool IsOpen() const
	{
		return MinExits <= 0 && MaxExits <= 0 && Transitions == EMazeRoomTransitions::Any;
	}
};

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	EMazeRuleTarget Target = EMazeRuleTarget::Type;

	/** Which type in the object library. Must match a TypeId there exactly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule",
		meta = (EditCondition = "Target == EMazeRuleTarget::Type", EditConditionHides))
	FName TypeId;

	/** Every type of this category in the library is fair game. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule",
		meta = (EditCondition = "Target == EMazeRuleTarget::Category", EditConditionHides))
	EMazeObjectCategory Category = EMazeObjectCategory::Decor;

	/** Which rooms this rule is for. Left alone, it is every room. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	FMazeRoomFilter Rooms;

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

	/**
	 *  What to call this rule in a message.
	 *
	 *  A rule naming a category has no TypeId, and every log line that printed one would say
	 *  'None' — the most useless word a diagnostic can contain, and one that reads as a bug in
	 *  the rule rather than a rule of a different shape.
	 */
	FString Describe() const;
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

	/**
	 *  The rules worth running, in order.
	 *
	 *  Disabled ones are skipped, and so is a rule that targets a type without naming one — an
	 *  empty TypeId can match nothing, and passing it on would only produce an "unknown type"
	 *  complaint about a name that was never typed. A rule targeting a category always has one.
	 */
	void GetActiveRules(TArray<FMazeSpawnRule>& OutRules) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
