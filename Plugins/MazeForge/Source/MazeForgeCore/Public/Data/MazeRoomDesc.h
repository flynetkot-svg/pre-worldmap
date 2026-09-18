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

/**
 *  Which room a cell belongs to.
 *
 *  One question, asked from four places — the slicer while it builds the portal graph, the
 *  exporter while it buckets placements, the mode while it highlights the room under the
 *  cursor, and now the generator, which asks it of every candidate cell. Until this existed
 *  each caller wrote the search out again: a protected static in the slicer that nothing
 *  outside it could reach, and two copies of the same FindByPredicate elsewhere. That is the
 *  shape of divergence the mesh and level names were pulled into a shared helper to avoid.
 *
 *  Linear, and deliberately so. Rooms are a handful per maze — 208 in the largest so far —
 *  and a rectangle test is two comparisons. An index would be a structure to keep in sync
 *  with a list that is rebuilt by every re-slice.
 */
namespace MazeRooms
{
	/** Index into Rooms, or INDEX_NONE when the cell is outside every room. */
	MAZEFORGECORE_API int32 IndexAtXZ(const TArray<FMazeRoomDesc>& Rooms, int32 X, int32 Z);

	/** The room itself, or null. The pointer dies with the next re-slice; do not keep it. */
	MAZEFORGECORE_API const FMazeRoomDesc* FindAtXZ(const TArray<FMazeRoomDesc>& Rooms,
	                                                const FIntPoint& CellXZ);
}
