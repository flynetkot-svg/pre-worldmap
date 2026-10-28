#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MazeLockComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMazeLockOpened, AActor*, Opener, bool, bRestored);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMazeLockRefused, AActor*, Toucher);

/**
 *  Makes its actor a locked door. Goes on the door's Blueprint.
 *
 *  Touched by anything carrying a Maze Keyring with the right key, it opens: On Unlocked fires
 *  and the Blueprint plays the animation and turns its blocking collision off. Touched without
 *  the key, On Refused fires — the place for "you need the red key". A door opened in this
 *  session is open again, with bRestored set, when its room streams back in.
 *
 *  The component does not move anything itself: how a door opens is the door's business.
 *  The actor needs a trigger that overlaps the player in front of the door.
 */
UCLASS(ClassGroup = "MazeForge", meta = (BlueprintSpawnableComponent, DisplayName = "Maze Lock"))
class MAZEFORGECORE_API UMazeLockComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMazeLockComponent();

	/** Which key opens it. Empty — taken from the placement (Maze Object Id) at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lock")
	FName LockId;

	/** Open on touch when the key is on the ring. Off — call TryUnlock yourself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lock")
	bool bUnlockOnTouch = true;

	/** Take the key off the ring when the door opens. Off — one key opens every door of its colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Lock")
	bool bConsumeKey = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MazeForge|Lock")
	bool bLocked = true;

	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Lock")
	FOnMazeLockOpened OnUnlocked;

	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Lock")
	FOnMazeLockRefused OnRefused;

	/** Opens if Opener carries the key. True if the door is open afterwards. */
	UFUNCTION(BlueprintCallable, Category = "MazeForge|Lock")
	bool TryUnlock(AActor* Opener);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UFUNCTION()
	void HandleOverlap(AActor* OverlappedActor, AActor* OtherActor);

	void Open(AActor* Opener, bool bRestored);
};
