#pragma once

#include "CoreMinimal.h"
#include "MazeStreamTypes.generated.h"

/** How the player is moving right now. Determines where the pool looks ahead. */
UENUM(BlueprintType)
enum class EMazeMotionMode : uint8
{
	Walking = 0,
	Falling = 1,
	/** Climbing a ladder. Reserved: there are no ladders in the project yet. */
	Climbing = 2,
	Flying = 3
};

/**
 *  Snapshot of the observer state the rules use to compute load priorities.
 *
 *  The camera is deliberately kept separate from the player: it is offset from him
 *  along Y and has its own frame in XZ, so loading must follow what is visible, not
 *  where the pawn stands.
 */
USTRUCT(BlueprintType)
struct MAZEFORGESTREAMING_API FMazeStreamQuery
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Stream")
	FVector PlayerLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Stream")
	FVector PlayerVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Stream")
	FVector CameraLocation = FVector::ZeroVector;

	/** Half of the visible camera frame in the XZ plane, in centimetres. */
	UPROPERTY(BlueprintReadOnly, Category = "Stream")
	FVector2D CameraHalfExtentXZ = FVector2D(1920.0, 1080.0);

	UPROPERTY(BlueprintReadOnly, Category = "Stream")
	EMazeMotionMode MotionMode = EMazeMotionMode::Walking;

	UPROPERTY(BlueprintReadOnly, Category = "Stream")
	FName CurrentRoomId;

	UPROPERTY(BlueprintReadOnly, Category = "Stream")
	float GravityZ = -980.0f;
};
