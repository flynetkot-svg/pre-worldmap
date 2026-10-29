#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "MazeRopeAnchorComponent.generated.h"

/**
 *  Where a rope comes out of its hook. Goes on the hook's (or post's) Blueprint, moved to the
 *  knot — the eye of the hook, the top of the post.
 *
 *  A rope looks for one of these near each of its ends and runs a tie from the edge of the gap
 *  up to it, so the rope comes out of the hook instead of ending in the air at the ledge. No
 *  anchor near an end — that end simply stops at the edge.
 */
UCLASS(ClassGroup = "MazeForge", meta = (BlueprintSpawnableComponent, DisplayName = "Maze Rope Anchor"))
class MAZEFORGECORE_API UMazeRopeAnchorComponent : public USceneComponent
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};

/** The rope anchors loaded right now, so a rope can find the ones by its ends — in any room. */
UCLASS()
class MAZEFORGECORE_API UMazeRopeAnchorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void Register(UMazeRopeAnchorComponent* Anchor) { Anchors.AddUnique(Anchor); }
	void Unregister(UMazeRopeAnchorComponent* Anchor) { Anchors.Remove(Anchor); }

	/** The anchor nearest Point within Radius cm, or null. */
	UMazeRopeAnchorComponent* FindNear(const FVector& Point, float Radius) const;

	static UMazeRopeAnchorSubsystem* Get(const UObject* WorldContext);

private:
	TArray<TWeakObjectPtr<UMazeRopeAnchorComponent>> Anchors;
};
