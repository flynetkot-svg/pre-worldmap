#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MazeDoorComponent.generated.h"

class APawn;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMazeDoorMoved);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMazeDoorPawn, APawn*, Pawn);

/**
 *  Makes its actor a sliding door. Goes on the door's Blueprint.
 *
 *  Builds the door itself: a panel of Door Mesh stretched to the door's size — as tall as it
 *  was placed, as deep as the Play band — and a zone Trigger Distance to either side. Someone
 *  in the zone and the panel rides up into the ceiling; the zone empty for Close Delay and it
 *  comes down. Never on anyone: while the zone holds a pawn it stays up.
 *
 *  With a Lock Id it is locked: it opens only for someone carrying that key, takes the key,
 *  and from then on opens for anyone — for the rest of the session, its room reloaded or not.
 */
UCLASS(ClassGroup = "MazeForge", meta = (BlueprintSpawnableComponent, DisplayName = "Maze Door"))
class MAZEFORGECORE_API UMazeDoorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMazeDoorComponent();

	// ------------------------------------------------------------------ shape

	/** Width (X), depth (Y), height (Z) in cm. Zero — from the placement, which is how the export sets it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Door")
	FVector Size = FVector::ZeroVector;

	/** The panel, stretched to Size. Empty — the engine cube. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Door")
	TObjectPtr<UStaticMesh> DoorMesh;

	/** Instead of the mesh's own materials. Empty — keep them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Door")
	TObjectPtr<UMaterialInterface> DoorMaterial;

	/**
	 *  How much thinner than the doorway the panel is on each side, across (X) and in depth (Y),
	 *  in cm. Sliding up, the panel goes into the rock; faces that lie exactly on the rock's own
	 *  flicker (z-fighting). A centimetre or two keeps them apart.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Door", meta = (ClampMin = "0", Units = "cm"))
	float SurfaceInset = 1.5f;

	/** Hide the panel while it is all the way up, inside the rock: nothing of it to see there. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Door")
	bool bHideWhenOpen = true;

	// ------------------------------------------------------------------ motion

	/** How fast it rides up, in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Door", meta = (ClampMin = "1", Units = "cm/s"))
	float OpenSpeed = 400.0f;

	/** How fast it comes down, in cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Door", meta = (ClampMin = "1", Units = "cm/s"))
	float CloseSpeed = 250.0f;

	/** How far up it goes, as a share of its height. 1 — all the way into the ceiling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Door", meta = (ClampMin = "0.1", ClampMax = "2"))
	float OpenFraction = 1.0f;

	/** How long the zone stays empty before it comes down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Door", meta = (ClampMin = "0", Units = "s"))
	float CloseDelay = 0.5f;

	/** How far either side of the door someone opens it, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Door", meta = (ClampMin = "0", Units = "cm"))
	float TriggerDistance = 150.0f;

	/** Only players open it. Off — any pawn: enemies too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Door")
	bool bOnlyPlayers = true;

	// ------------------------------------------------------------------ lock

	/** The key it wants. Empty — from the placement (a type with Lock Role Door); still empty — no lock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Door|Lock")
	FName LockId;

	/** The key leaves the ring when it opens the door. Off — one key opens every door of its colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Door|Lock")
	bool bConsumeKey = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MazeForge|Door|Lock")
	bool bLocked = false;

	// ------------------------------------------------------------------ state and events

	/** 0 — shut, 1 — all the way up. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MazeForge|Door")
	float OpenAmount = 0.0f;

	/** Starts riding up. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Door")
	FOnMazeDoorMoved OnOpening;

	/** Starts coming down. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Door")
	FOnMazeDoorMoved OnClosing;

	/** Shut, down on the floor. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Door")
	FOnMazeDoorMoved OnClosed;

	/** Unlocked with a key — once. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Door")
	FOnMazeDoorPawn OnUnlocked;

	/** Someone came without the key. Once each time they come. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Door")
	FOnMazeDoorPawn OnRefused;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Rebuilds the panel at the size it has now — the export calls it once it has told the door its size. */
	void RefreshPreview();

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Panel;

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> Zone;

	/** Where the panel sits shut, relative to this component. */
	FVector PanelBase = FVector::ZeroVector;

	/** The size in use: Size, else the placement's, else a stand-in. Size itself is left as set. */
	FVector Resolved = FVector::ZeroVector;
	FVector ResolveSize(bool bWarn) const;

	float EmptyTime = 0.0f;
	int32 Direction = 0;   // +1 up, -1 down, 0 still
	TArray<TWeakObjectPtr<APawn>> Refused;

	void BuildPanel();
	void BuildZone();
	bool Wants(APawn* Pawn);
};
