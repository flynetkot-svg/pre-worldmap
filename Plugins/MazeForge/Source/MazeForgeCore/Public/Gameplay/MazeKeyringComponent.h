#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MazeKeyringComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMazeKeyAdded, FName, LockId);

/**
 *  The keys the player carries. Goes on the character.
 *
 *  Holds Lock Ids, not key actors: a red key is the right to open red doors, and once picked up
 *  the actor that represented it is gone. A Maze Key adds to it on touch, a Maze Lock reads it.
 */
UCLASS(ClassGroup = "MazeForge", meta = (BlueprintSpawnableComponent, DisplayName = "Maze Keyring"))
class MAZEFORGECORE_API UMazeKeyringComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMazeKeyringComponent();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MazeForge|Keys")
	TArray<FName> Keys;

	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Keys")
	FOnMazeKeyAdded OnKeyAdded;

	/** Adds a key. Returns false if it was already on the ring. */
	UFUNCTION(BlueprintCallable, Category = "MazeForge|Keys")
	bool AddKey(FName LockId);

	UFUNCTION(BlueprintPure, Category = "MazeForge|Keys")
	bool HasKey(FName LockId) const;

	/** For a door that eats its key. Returns false if there was none. */
	UFUNCTION(BlueprintCallable, Category = "MazeForge|Keys")
	bool RemoveKey(FName LockId);

	/** The keyring on an actor, or null. */
	static UMazeKeyringComponent* Find(const AActor* Actor);
};
