#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/MazeTypes.h"
#include "MazeRoomAnchor.generated.h"

/**
 *  Marker of a room inside its own level.
 *
 *  It deliberately lives in the Runtime module: the actor is saved into the .umap and
 *  must exist in the game build. It holds everything one needs to know about the room
 *  on the spot — identifier, bounds, neighbours and the depth layout — so a designer
 *  who opens the room level away from the maze still sees the context.
 */
UCLASS()
class MAZEFORGECORE_API AMazeRoomAnchor : public AActor
{
	GENERATED_BODY()

public:
	AMazeRoomAnchor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Room")
	FName RoomId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Room")
	FBox WorldBounds = FBox(ForceInit);

	/** Rooms this one has a passable connection to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Room")
	TArray<FName> Neighbors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Room")
	FMazeDepthProfile Depth;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Room")
	FIntPoint MinXZ = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Room")
	FIntPoint MaxXZ = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze Room")
	int32 EstimatedTriangles = 0;
};
