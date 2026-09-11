#pragma once

#include "CoreMinimal.h"
#include "Rules/MazeStreamingRule.h"
#include "MazeRule_CameraFrame.generated.h"

/**
 *  The base rule: everything that falls into the camera frame must be loaded.
 *
 *  It is computed from the camera, not from the player. The camera is a dozen metres
 *  away from him and has its own view rectangle — if we loaded by the pawn's position,
 *  a room would finish "drawing itself in" while already in frame at the far edge of
 *  the screen.
 */
UCLASS(DisplayName = "Camera Frame")
class MAZEFORGESTREAMING_API UMazeRule_CameraFrame : public UMazeStreamingRule
{
	GENERATED_BODY()

public:
	/** The margin around the frame. A room inside it counts as mandatory. */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", Units = "cm"))
	float MarginUU = 800.0f;

	/** At what distance from the frame the desire falls to zero. */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", Units = "cm"))
	float FalloffUU = 4000.0f;

	virtual void Score(const FMazeStreamQuery& Query,
	                   const UMazeWorldManifest& Manifest,
	                   TMap<FName, float>& InOutDesire) const override;

	virtual FText GetDisplayName() const override;
};
