#pragma once

#include "CoreMinimal.h"
#include "Rules/MazeStreamingRule.h"
#include "MazeRule_VerticalMotion.generated.h"

/**
 *  The vertical: falling and climbing.
 *
 *  While falling, the ordinary velocity prediction lies — it is linear, while a fall
 *  accelerates. We compute the landing point from energy: h = v^2 / 2g.
 *  The ladder-climbing branch is ready, but there are no ladders in the project yet.
 */
UCLASS(DisplayName = "Vertical Motion")
class MAZEFORGESTREAMING_API UMazeRule_VerticalMotion : public UMazeStreamingRule
{
	GENERATED_BODY()

public:
	/** How far down to look while falling, in room heights. */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", Units = "cm"))
	float MaxFallProbeUU = 6000.0f;

	/** The margin up and down while climbing a ladder. */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", Units = "cm"))
	float ClimbProbeUU = 3000.0f;

	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", Units = "cm"))
	float FalloffUU = 3000.0f;

	virtual void Score(const FMazeStreamQuery& Query,
	                   const UMazeWorldManifest& Manifest,
	                   TMap<FName, float>& InOutDesire) const override;

	virtual FText GetDisplayName() const override;
};
