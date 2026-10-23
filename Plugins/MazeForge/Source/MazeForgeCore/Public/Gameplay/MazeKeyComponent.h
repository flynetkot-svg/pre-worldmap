#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MazeKeyComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMazeKeyPickedUp, AActor*, Collector);

/**
 *  Makes its actor a key. Goes on the key's Blueprint.
 *
 *  Touch it with anything that carries a Maze Keyring and the key goes on the ring, On Picked Up
 *  fires — the place for a sound and a flash — and the actor goes away. A key already picked up
 *  in this session does not come back when its room streams in again.
 *
 *  The actor needs a collision that overlaps the player; the component only listens to it.
 */
UCLASS(ClassGroup = "MazeForge", meta = (BlueprintSpawnableComponent, DisplayName = "Maze Key"))
class MAZEFORGECORE_API UMazeKeyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMazeKeyComponent();

	/** Which doors it opens. Empty — taken from the placement (Maze Object Id) at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Key")
	FName LockId;

	/** Pick up on touch. Off — call TryPickUp yourself, from an interaction for instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Key")
	bool bPickUpOnTouch = true;

	/** Destroy the actor once picked up. Off — hide it yourself in On Picked Up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Key")
	bool bDestroyOnPickUp = true;

	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Key")
	FOnMazeKeyPickedUp OnPickedUp;

	/** Puts the key on Collector's keyring. False if he has none or the key is already taken. */
	UFUNCTION(BlueprintCallable, Category = "MazeForge|Key")
	bool TryPickUp(AActor* Collector);

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleOverlap(AActor* OverlappedActor, AActor* OtherActor);

	bool bTaken = false;
};
