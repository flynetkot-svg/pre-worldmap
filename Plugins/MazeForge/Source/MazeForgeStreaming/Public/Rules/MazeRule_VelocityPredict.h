#pragma once

#include "CoreMinimal.h"
#include "Rules/MazeStreamingRule.h"
#include "MazeRule_VelocityPredict.generated.h"

/**
 *  Prediction from velocity: we load where the player is going to run.
 *
 *  The prediction horizon grows together with the speed — the faster the player runs,
 *  the further ahead we have to look, otherwise a room will not manage to load while
 *  he accelerates.
 */
UCLASS(DisplayName = "Velocity Predict")
class MAZEFORGESTREAMING_API UMazeRule_VelocityPredict : public UMazeStreamingRule
{
	GENERATED_BODY()

public:
	/** The base prediction horizon. */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", Units = "s"))
	float LookAheadSeconds = 1.5f;

	/** How far the horizon stretches at maximum speed. */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", Units = "s"))
	float MaxLookAheadSeconds = 3.0f;

	/** The speed at which the horizon reaches its maximum. */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1"))
	float ReferenceSpeed = 600.0f;

	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", Units = "cm"))
	float FalloffUU = 4000.0f;

	virtual void Score(const FMazeStreamQuery& Query,
	                   const UMazeWorldManifest& Manifest,
	                   TMap<FName, float>& InOutDesire) const override;

	virtual FText GetDisplayName() const override;
};
