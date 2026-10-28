#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MazeProgressSubsystem.generated.h"

/**
 *  What the player has used up, by placement id: the keys he picked up, the doors he opened.
 *
 *  Rooms stream in and out, and every time a room's level loads its actors are spawned fresh
 *  from the level — the key he took ten minutes ago would be lying there again, the door shut
 *  again. The id the export stamps on each object (UMazeObjectIdComponent) is what lets a key
 *  or a door ask on BeginPlay whether it is already spent.
 *
 *  Per world, so per session of play. Carrying it into a save game is the game's business; the
 *  set is public for exactly that.
 */
UCLASS()
class MAZEFORGECORE_API UMazeProgressSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "MazeForge|Progress")
	void MarkUsed(int32 PlacementId);

	UFUNCTION(BlueprintPure, Category = "MazeForge|Progress")
	bool IsUsed(int32 PlacementId) const;

	/** Every placement id used so far. For a save game to store and to restore with MarkUsed. */
	UPROPERTY(BlueprintReadOnly, Category = "MazeForge|Progress")
	TSet<int32> UsedPlacements;

	/** Remembers a number for a placement — which stop a lift is at. Zero placement id is ignored. */
	UFUNCTION(BlueprintCallable, Category = "MazeForge|Progress")
	void SetState(int32 PlacementId, int32 State);

	/** The number remembered for a placement. False if none was. */
	UFUNCTION(BlueprintPure, Category = "MazeForge|Progress")
	bool GetState(int32 PlacementId, int32& OutState) const;

	/** Every remembered number, by placement id. For a save game, like Used Placements. */
	UPROPERTY(BlueprintReadOnly, Category = "MazeForge|Progress")
	TMap<int32, int32> PlacementStates;

	/** Null-safe lookup from any actor. */
	static UMazeProgressSubsystem* Get(const UObject* WorldContext);
};
