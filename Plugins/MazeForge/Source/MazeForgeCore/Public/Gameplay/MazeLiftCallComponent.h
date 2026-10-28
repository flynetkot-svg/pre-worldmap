#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MazeLiftCallComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMazeLiftCalled, int32, Stop);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMazeLiftCallRefused);

/**
 *  Makes its actor a lift call terminal. Goes on the terminal's Blueprint.
 *
 *  Call brings its lift to its stop — wire it to the player's interact button in the
 *  Blueprint. Which lift and which stop are stamped by the export; the lift is found among the
 *  lifts loaded right now, so it can stand in another room's level.
 */
UCLASS(ClassGroup = "MazeForge", meta = (BlueprintSpawnableComponent, DisplayName = "Maze Lift Call"))
class MAZEFORGECORE_API UMazeLiftCallComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMazeLiftCallComponent();

	/** Which lift it calls. Empty — taken from the placement (Maze Object Id). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift")
	FName LiftId;

	/** Which stop it calls the lift to. -1 — from the placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift")
	int32 Stop = -1;

	/** Call on touch. Off — call Call yourself, from an interaction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift")
	bool bCallOnTouch = false;

	/** The lift is on its way, or already here. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Lift")
	FOnMazeLiftCalled OnCalled;

	/** It would not come: not loaded, no such stop, or busy riding. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Lift")
	FOnMazeLiftCallRefused OnRefused;

	/** Calls the lift to this terminal's stop. True if it is coming or already here. */
	UFUNCTION(BlueprintCallable, Category = "MazeForge|Lift")
	bool Call();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UFUNCTION()
	void HandleOverlap(AActor* OverlappedActor, AActor* OtherActor);
};
