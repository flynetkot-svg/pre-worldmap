#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "MazeLiftComponent.generated.h"

class APawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMazeLiftStop, int32, Stop);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMazeLiftCrush, APawn*, Victim);

/**
 *  Makes its actor a lift platform. Goes on the platform's Blueprint.
 *
 *  Rides the actor up and down between its stops — heights above where the export spawned it,
 *  the lower stop first — at Speed. Whoever stands on it rides along: a character standing on a
 *  moving primitive is carried by its movement component, so the root needs Movable mobility and
 *  collision the character can stand on. Terminals with a Maze Lift Call component call it to
 *  their stop; a button on the platform itself can call Move To Other Stop.
 *
 *  Which stop it waits at survives its room streaming out and back in.
 */
UCLASS(ClassGroup = "MazeForge", meta = (BlueprintSpawnableComponent, DisplayName = "Maze Lift"))
class MAZEFORGECORE_API UMazeLiftComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMazeLiftComponent();

	/** The name its terminals call it by. Empty — taken from the placement (Maze Object Id). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift")
	FName LiftId;

	/** Heights of the stops above where it was spawned, in cm, lowest first. Empty — from the placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift")
	TArray<float> Stops;

	/** The stop it waits at when the game starts. -1 — from the placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift")
	int32 StartStop = -1;

	/** How fast it rides, in cm per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift", meta = (ClampMin = "1", Units = "cm/s"))
	float Speed = 200.0f;

	/**
	 *  Call, then board: a terminal's call at the stop the lift stands at — or the lift arriving
	 *  at a terminal's call — readies it, and a player standing on it for Board Delay then rides
	 *  it to the other stop. Stepping on without a call does nothing. Off — it moves only when
	 *  called (terminals, Move To Stop).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift")
	bool bRideWhenBoarded = true;

	/** How long a player stands on the readied lift before it sets off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift",
		meta = (EditCondition = "bRideWhenBoarded", ClampMin = "0", Units = "s"))
	float BoardDelay = 0.75f;

	/** A call while it rides is ignored. Off — it turns round for the new stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift")
	bool bIgnoreCallsWhileMoving = true;

	/**
	 *  Riding down, it crushes whoever is under it: a pawn in its way gets Crush Damage and On
	 *  Crushed. Off — it passes through, and the character's movement shoves them aside.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift|Crush")
	bool bCrushes = true;

	/**
	 *  Damage dealt by Apply Damage to a crushed pawn — the usual Any Damage / Take Damage
	 *  route. The default kills anything; 0 — only On Crushed, the Blueprint decides.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift|Crush",
		meta = (EditCondition = "bCrushes", ClampMin = "0"))
	float CrushDamage = 10000.0f;

	/** How close under the platform counts as under it, in cm: the head touching it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift|Crush",
		meta = (EditCondition = "bCrushes", ClampMin = "0", Units = "cm"))
	float CrushReach = 5.0f;

	/**
	 *  Cut off each side of the platform's footprint, in cm, so someone brushing past its edge,
	 *  on a ledge beside the shaft, is not under it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lift|Crush",
		meta = (EditCondition = "bCrushes", ClampMin = "0", Units = "cm"))
	float CrushInset = 10.0f;

	/** A pawn caught under it. Once per pawn per ride. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Lift")
	FOnMazeLiftCrush OnCrushed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MazeForge|Lift")
	int32 CurrentStop = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MazeForge|Lift")
	bool bMoving = false;

	/** Called and standing at that stop: the next player to board rides it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MazeForge|Lift")
	bool bReady = false;

	/** It sets off; the stop it is heading for. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Lift")
	FOnMazeLiftStop OnDeparted;

	/** It has arrived at this stop. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Lift")
	FOnMazeLiftStop OnArrived;

	/**
	 *  A call to a stop: rides there, or is already there; either way it is then ready for a rider.
	 *  False if there is no such stop, or it is riding and ignores calls.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeForge|Lift")
	bool MoveToStop(int32 Stop);

	/** Sends it on: two stops — the other one, more — the next. Not a call: it arrives not ready. */
	UFUNCTION(BlueprintCallable, Category = "MazeForge|Lift")
	bool MoveToOtherStop();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	FVector BaseLocation = FVector::ZeroVector;
	bool bBaseKnown = false;
	float Height = 0.0f;
	int32 TargetStop = 0;

	/** How long the rider has stood on the readied lift. */
	float BoardTime = 0.0f;

	/** Whether arriving at TargetStop makes it ready: it does after a call, not after a ride. */
	bool bReadyOnArrival = false;

	/** Sets off for a stop. */
	bool StartRide(int32 Stop, bool bCalled);

	/** A player character standing on the platform. */
	bool HasRider() const;

	/** Pawns already crushed this ride. */
	TArray<TWeakObjectPtr<APawn>> Crushed;

	void PlaceAt(float NewHeight);

	/** Whoever is in the Drop cm under the platform, crushed. */
	void CrushBelow(float Drop);
};

/**
 *  The lifts loaded right now, by Lift Id: how a terminal finds its lift, which may stand in
 *  another room's level. A lift whose room is not loaded cannot be called.
 */
UCLASS()
class MAZEFORGECORE_API UMazeLiftSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void Register(UMazeLiftComponent* Lift);
	void Unregister(UMazeLiftComponent* Lift);

	/** The loaded lift with this Lift Id, or null. */
	UFUNCTION(BlueprintPure, Category = "MazeForge|Lift")
	UMazeLiftComponent* FindLift(FName LiftId) const;

	/** Null-safe lookup from any actor. */
	static UMazeLiftSubsystem* Get(const UObject* WorldContext);

private:
	TMap<FName, TWeakObjectPtr<UMazeLiftComponent>> Lifts;
};
