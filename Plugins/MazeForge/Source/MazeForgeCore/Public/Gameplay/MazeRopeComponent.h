#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Components/SplineMeshComponent.h"
#include "MazeRopeComponent.generated.h"

class ACharacter;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UMazeRopeAnchorComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMazeRopeRider, ACharacter*, Character);

/**
 *  Makes its actor a taut rope to run across. Goes on the rope's Blueprint.
 *
 *  The rope runs along the actor's X from its origin — the left end, where the export puts it —
 *  for Length cm. It is drawn as Rope Mesh pieces laid end to end along a spline, as many as fit
 *  its length, so the texture keeps its scale however long the rope is; the spline bends with a
 *  simulated string: it gives under the runner's feet, shivers with every step, and settles.
 *
 *  An invisible strip under it carries whoever runs across, and dips with it so the feet stay on
 *  the rope. Stand still on it for Fall Delay and you drop through.
 */
UCLASS(ClassGroup = "MazeForge", meta = (BlueprintSpawnableComponent, DisplayName = "Maze Rope"))
class MAZEFORGECORE_API UMazeRopeComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMazeRopeComponent();

	// ------------------------------------------------------------------ shape

	/** Length in cm, along X. 0 — from the placement (Maze Object Id), which is how the export sets it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Rope", meta = (ClampMin = "0", Units = "cm"))
	float Length = 0.0f;

	/**
	 *  One piece of rope, laid end to end along the spline. Model it along Mesh Axis with the
	 *  texture running once along its length; its length along that axis is how often the
	 *  texture repeats. Empty — the engine cylinder.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Rope|Look")
	TObjectPtr<UStaticMesh> RopeMesh;

	/** Its long axis. The engine cylinder stands along Z. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Rope|Look")
	TEnumAsByte<ESplineMeshAxis::Type> MeshAxis = ESplineMeshAxis::X;

	/** Instead of the mesh's own materials. Empty — keep them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Rope|Look")
	TObjectPtr<UMaterialInterface> RopeMaterial;

	/** How thick the rope is drawn, in cm, whatever the mesh's own thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Rope|Look", meta = (ClampMin = "0.1", Units = "cm"))
	float Thickness = 3.0f;

	/** One piece stretched over the whole length: the texture stretches with it. Off — pieces end to end. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Rope|Look")
	bool bStretchSingleMesh = false;

	// ------------------------------------------------------------------ string

	/** How far it hangs in the middle with nobody on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Rope|String", meta = (ClampMin = "0", Units = "cm"))
	float RestSag = 4.0f;

	/** How far it gives under the runner's feet in the middle of the span; less near the ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Rope|String", meta = (ClampMin = "0", Units = "cm"))
	float SagUnderFeet = 16.0f;

	/** How fast a shiver runs along it. Higher — a tauter, higher-pitched rope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Rope|String", meta = (ClampMin = "100", Units = "cm/s"))
	float WaveSpeed = 900.0f;

	/** How fast a shiver dies away, per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Rope|String", meta = (ClampMin = "0"))
	float Damping = 1.6f;

	/** The kick of one step, in cm/s of the rope under the foot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Rope|String", meta = (ClampMin = "0", Units = "cm/s"))
	float StepKick = 130.0f;

	/** One step every this many cm run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Rope|String", meta = (ClampMin = "10", Units = "cm"))
	float StepDistance = 70.0f;

	/** The kick of someone landing on it, or dropping off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Rope|String", meta = (ClampMin = "0", Units = "cm/s"))
	float BoardKick = 260.0f;

	/** How wide a kick is: the length of rope a footfall or a landing throws, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Rope|String", meta = (ClampMin = "10", Units = "cm"))
	float KickWidth = 60.0f;

	// ------------------------------------------------------------------ ties

	/**
	 *  How far from each end of the rope to look for a Maze Rope Anchor — the knot on the hook.
	 *  Found — a tie runs from the edge of the gap up to it. 0 — no ties.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Rope|Look", meta = (ClampMin = "0", Units = "cm"))
	float AnchorSearchRadius = 250.0f;

	// ------------------------------------------------------------------ falling

	/** Slower than this counts as standing still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Rope|Fall", meta = (ClampMin = "0", Units = "cm/s"))
	float StopSpeed = 60.0f;

	/** Standing still this long, and the rope gives way under him. 0 — he never falls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Rope|Fall", meta = (ClampMin = "0", Units = "s"))
	float FallDelay = 0.3f;

	/** How deep the strip that carries him is, across the level (Y), in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MazeForge|Rope|Fall", meta = (ClampMin = "1", Units = "cm"))
	float CollisionDepth = 80.0f;

	/** Someone stepped onto it. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Rope")
	FOnMazeRopeRider OnBoarded;

	/** Someone stood still on it and dropped through. */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge|Rope")
	FOnMazeRopeRider OnFell;

	/** How far the rope is below its straight line at X cm from the left end, right now, in cm. */
	UFUNCTION(BlueprintPure, Category = "MazeForge|Rope")
	float GetSagAt(float X) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FRider
	{
		TWeakObjectPtr<ACharacter> Character;
		float LastX = 0.0f;
		float StillTime = 0.0f;
		float SinceStep = 0.0f;
		bool bSeen = false;
	};

	struct FDropped
	{
		TWeakObjectPtr<ACharacter> Character;
		float Time = 0.0f;
	};

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> Strip;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> Pieces;

	/** The ties from the ends of the gap to the anchors: left, then right. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> TiePieces;

	TWeakObjectPtr<UMazeRopeAnchorComponent> TieAnchor[2];
	float TieSearchTimer = 0.0f;

	/** Look per mesh: the scale across, and one piece's length along the axis. */
	float PieceScale = 1.0f;
	float PieceLength = 100.0f;

	TArray<FRider> Riders;
	TArray<FDropped> Dropped;

	/** The shiver: displacement and its speed at evenly spaced nodes, ends fixed. */
	TArray<float> Wave;
	TArray<float> WaveSpeedAt;
	float NodeStep = 0.0f;

	/** The dip under the feet: where, and how much of it (0..1, eased). */
	float LoadX = 0.0f;
	float LoadAmount = 0.0f;
	float LoadTarget = 0.0f;

	bool bAsleep = false;

	void Build();
	USplineMeshComponent* MakePiece();
	void FindTies();
	void Kick(float X, float Speed);
	float StaticSag(float X) const;
	float WaveAt(float X) const;
	float HeightAt(float X) const { return -GetSagAt(X); }
	void UpdateRiders(float DeltaTime);
	void Drop(ACharacter* Character);
	void UpdateDropped(float DeltaTime);
	void Simulate(float DeltaTime);
	void UpdatePieces();
};
