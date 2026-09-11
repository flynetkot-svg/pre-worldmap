#pragma once

#include "CoreMinimal.h"
#include "MazeRoomDesc.generated.h"

/**
 *  Description of a single streaming room.
 *
 *  A room is defined only by a rectangle in XZ and always takes the whole depth:
 *  the player is locked in the XZ plane and does not move along Y, so there is
 *  no point in cutting the depth up.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeRoomDesc
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FName RoomId;

	/** Inclusive. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FIntPoint MinXZ = FIntPoint::ZeroValue;

	/** Exclusive. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FIntPoint MaxXZ = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	int32 SolidCellCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FBox WorldBounds = FBox(ForceInit);

	/** Rooms this one has a passable connection to. Filled in by the slicer. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	TArray<FName> Neighbors;

	bool ContainsXZ(int32 X, int32 Z) const
	{
		return X >= MinXZ.X && X < MaxXZ.X && Z >= MinXZ.Y && Z < MaxXZ.Y;
	}

	FIntPoint SizeXZ() const { return MaxXZ - MinXZ; }

	bool IsValid() const { return !RoomId.IsNone() && MaxXZ.X > MinXZ.X && MaxXZ.Y > MinXZ.Y; }
};
