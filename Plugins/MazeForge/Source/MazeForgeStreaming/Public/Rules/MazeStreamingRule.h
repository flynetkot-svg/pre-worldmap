#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MazeStreamTypes.h"
#include "MazeStreamingRule.generated.h"

class UMazeWorldManifest;

/**
 *  A load priority rule.
 *
 *  Every rule writes into the shared map its "desire" to keep a room loaded, in the
 *  range 0..1, multiplied by its own weight. The pool combines the contributions and
 *  decides what to load and what to unload.
 *
 *  A new rule = one derived class added to the list in the settings asset.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class MAZEFORGESTREAMING_API UMazeStreamingRule : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Rule")
	bool bEnabled = true;

	virtual void Score(const FMazeStreamQuery& Query,
	                   const UMazeWorldManifest& Manifest,
	                   TMap<FName, float>& InOutDesire) const
		PURE_VIRTUAL(UMazeStreamingRule::Score, );

	virtual FText GetDisplayName() const;

protected:
	/** Adds the weighted contribution: the strongest rule wins, not a sum of noise. */
	void Accumulate(TMap<FName, float>& InOutDesire, FName RoomId, float RawScore) const;

	/**
	 *  The distance from a point to a room box, computed in XZ only.
	 *
	 *  The depth is ignored on purpose: the player is locked into the XZ plane and does
	 *  not move along Y, while the rooms take up the whole depth. Taking Y into account
	 *  would mean adding one and the same constant to every distance.
	 */
	static float DistanceToRoomXZ(const FVector& Point, const FBox& RoomBounds);
};
