#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "MazeFallDamageComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMazeFall, float, Height, float, Damage, bool, bLethal);

/**
 *  Fall damage. Goes on the player's Character Blueprint.
 *
 *  Measures every fall from its highest point — the top of the jump — to where it lands. Up to
 *  Safe Height nothing happens; from there to Lethal Height the damage grows from Min Damage to
 *  Max Damage; from Lethal Height on, Lethal Damage — death. A jump down a lift shaft, off a
 *  ledge, off a ladder: the same rule. Landing on a lift platform counts as landing.
 *
 *  The damage goes through Apply Damage (Any Damage / Take Damage, where the health is); On Fall
 *  is for the Blueprint either way — a landing animation, a sound, a camera shake.
 */
UCLASS(ClassGroup = "MazeForge", meta = (BlueprintSpawnableComponent, DisplayName = "Maze Fall Damage"))
class MAZEFORGECORE_API UMazeFallDamageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMazeFallDamageComponent();

	/** A fall up to this high does nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Fall", meta = (ClampMin = "0", Units = "cm"))
	float SafeHeight = 350.0f;

	/** A fall this high or higher kills. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Fall", meta = (ClampMin = "0", Units = "cm"))
	float LethalHeight = 1000.0f;

	/** Damage just over Safe Height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Fall", meta = (ClampMin = "0"))
	float MinDamage = 10.0f;

	/** Damage just under Lethal Height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Fall", meta = (ClampMin = "0"))
	float MaxDamage = 80.0f;

	/** Damage from Lethal Height on. The default kills anything. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Fall", meta = (ClampMin = "0"))
	float LethalDamage = 10000.0f;

	/** Off — only On Fall; the Blueprint applies what it likes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Fall")
	bool bApplyDamage = true;

	/** Landed from a fall higher than Safe Height. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Fall")
	FOnMazeFall OnFall;

	/** What a fall from this high costs: 0 up to Safe Height. */
	UFUNCTION(BlueprintPure, Category = "MazeForge|Fall")
	float DamageForHeight(float Height) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	bool bFalling = false;
	double ApexZ = 0.0;

	UFUNCTION()
	void HandleLanded(const FHitResult& Hit);
};
