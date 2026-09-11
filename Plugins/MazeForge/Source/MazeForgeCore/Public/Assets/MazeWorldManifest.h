#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/MazeSpawnTypes.h"
#include "MazeWorldManifest.generated.h"

class UWorld;

/** One room in the manifest: everything the runtime needs and nothing more. */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeRoomEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FName RoomId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	TSoftObjectPtr<UWorld> Level;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	FBox WorldBounds = FBox(ForceInit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	TArray<FName> Neighbors;

	/** Triangle estimate: the pool uses it as the budget of what is loaded. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room")
	int32 EstimatedTriangles = 0;
};

/**
 *  One end of a transition between mazes, as the game sees it.
 *
 *  Written by the export from placements whose type carries a transition role. Keyed by the
 *  placement id — the same number the spawned Gate actor carries on its id component, which is
 *  what lets a door look itself up in the world graph without knowing anything about itself.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeTransitionPoint
{
	GENERATED_BODY()

	/** The placement id. The key, and the only identity this point has. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transition")
	int32 Id = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transition")
	EMazeTransitionRole Role = EMazeTransitionRole::None;

	/** Which library type placed it. Not an identity — several points share a type. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transition")
	FName TypeId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transition")
	FVector Location = FVector::ZeroVector;

	/**
	 *  Which way the arriving player faces, from the placement's own rotation.
	 *
	 *  Applied on arrival, and a character's movement overrides it the moment there is input —
	 *  which is correct. It matters for the frame the fade comes back on.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transition")
	FRotator Rotation = FRotator::ZeroRotator;

	/** The grid cell it sits in. Carried so the world map can draw it on the maze schematic. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Transition")
	FIntPoint CellXZ = FIntPoint::ZeroValue;
};

/**
 *  The boundary between the editor and the game.
 *
 *  The runtime sees only the manifest and knows nothing about the voxel grid —
 *  so neither the megabytes of cells nor the editor code end up in the shipping build.
 */
UCLASS(BlueprintType)
class MAZEFORGECORE_API UMazeWorldManifest : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	TArray<FMazeRoomEntry> Rooms;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	FBox WorldBounds = FBox(ForceInit);

	/** World Y of the plane the player moves in. Always the centre of the Play band. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	float PlayPlaneY = 0.0f;

	/** Both ends of every transition this maze offers. See FMazeTransitionPoint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Manifest")
	TArray<FMazeTransitionPoint> Transitions;

	const FMazeRoomEntry* FindRoom(FName RoomId) const;

	/** The transition point with that placement id, or null. */
	const FMazeTransitionPoint* FindTransition(int32 Id) const;

	/** The room containing a point. Compared in XZ: along Y the rooms do not differ. */
	FName FindRoomAt(const FVector& WorldLocation) const;
};
