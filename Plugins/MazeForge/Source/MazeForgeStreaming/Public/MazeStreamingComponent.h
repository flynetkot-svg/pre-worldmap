#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MazeStreamTypes.h"
#include "MazeStreamingComponent.generated.h"

class APlayerCameraManager;
class UMazeRoomPool;
class UMazeStreamingRulesAsset;
class UMazeWorldGraph;
class UMazeWorldManifest;

/** Fires once after a maze switch, when the room the observer stands in has finished loading. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMazeOnMazeReady);

/**
 *  The observer the pool follows. Attached to the player pawn.
 *
 *  The references to the manifest and the rules live here rather than in the project
 *  settings: this way the designer picks the maze and the streaming profile right in the
 *  character blueprint, and several sets can be kept side by side for comparison.
 */
UCLASS(ClassGroup = "MazeForge", meta = (BlueprintSpawnableComponent))
class MAZEFORGESTREAMING_API UMazeStreamingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMazeStreamingComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge")
	TSoftObjectPtr<UMazeWorldManifest> Manifest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge")
	TSoftObjectPtr<UMazeStreamingRulesAsset> Rules;

	/**
	 *  How this game's mazes join up. Needed only if there is more than one.
	 *
	 *  It is what a door asks when it is walked into. Leave it empty for a single-maze game —
	 *  nothing else reads it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge")
	TSoftObjectPtr<UMazeWorldGraph> WorldGraph;

	/**
	 *  Widen the horizontal bounds of the side-view camera to the size of the maze.
	 *
	 *  The template ASideScrollingCameraManager keeps the camera within
	 *  CameraXMinBounds / CameraXMaxBounds, and on a long level it runs into the
	 *  limit while the player moves off screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge")
	bool bDriveCameraBounds = true;

	/**
	 *  Switches this observer to another maze — the door at the end of one area opening into
	 *  the next.
	 *
	 *  The move is part of the call and not a step the game takes afterwards, because the order
	 *  matters: the pool decides what to load from where the observer IS. Switch without moving
	 *  and the player is left standing in the old maze's coordinates, outside every room of the
	 *  new one, and nothing loads to arrive into.
	 *
	 *  Give each maze its own `World Origin` on its grid asset. Nothing enforces it — the old
	 *  maze is fully unloaded before the new one arrives, so overlapping coordinates are not
	 *  fatal — but distinct origins make "where am I" answerable, and let both be held for a
	 *  moment if the transition wants a crossfade.
	 *
	 *  `OnMazeReady` fires when the room under the observer is loaded and visible. That is the
	 *  cue to fade back in; until then the player is standing in an empty world.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeForge",
		meta = (DisplayName = "Switch To Maze At Location", AdvancedDisplay = "bTeleportOwner"))
	void SwitchToMazeAtLocation(TSoftObjectPtr<UMazeWorldManifest> NewManifest,
	                            FVector EntryLocation, bool bTeleportOwner = true);

	/**
	 *  Walk through the transition this placement id names. The whole of a door, in one node.
	 *
	 *  The door passes its OWN id — `Get Placement Id (self)` — and nothing else. Where that
	 *  leads is the world graph's business, so the door holds no destination, and one BP_Door
	 *  serves every doorway in the game.
	 *
	 *  Returns false and says why if the graph has no link from this gate. The usual reason is
	 *  that the point was deleted and drawn again: it is a new point with a new number, and the
	 *  link still names the old one.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeForge")
	bool TakeTransition(int32 PlacementId);

	/**
	 *  The whole of a door in one node: who walked in, and which door it was.
	 *
	 *  `Traveller` is the Other Actor from the overlap, `Gate` is the door itself — wire Self to
	 *  it, or leave it, since it defaults to Self. Everything else it works out: the placement id
	 *  off the gate's own id component, the streaming component off the traveller.
	 *
	 *  It exists because the three-node version — find the component, check it, call it — has two
	 *  places to wire the wrong pin, and both of them fail *silently*: a Blueprint call on a null
	 *  target simply does not happen, and an Is Valid in front of it turns that into a branch
	 *  nobody sees. A door that does nothing and says nothing is the hardest kind to fix, so this
	 *  one says something on every path it can fail on.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeForge",
		meta = (DisplayName = "Take Transition For", DefaultToSelf = "Gate"))
	static bool TakeTransitionFor(AActor* Traveller, AActor* Gate);

	/**
	 *  Switch to a maze, arriving at one of its transition points.
	 *
	 *  What TakeTransition calls once the graph has answered. Useful on its own for a scripted
	 *  jump that is not a door.
	 *
	 *  The destination manifest is loaded synchronously to read the point — a small data asset
	 *  with no geometry in it; the room levels it names are still streamed as usual.
	 */
	UFUNCTION(BlueprintCallable, Category = "MazeForge",
		meta = (DisplayName = "Switch To Maze At Transition", AdvancedDisplay = "bTeleportOwner"))
	void SwitchToMazeAtTransition(TSoftObjectPtr<UMazeWorldManifest> NewManifest, int32 TransitionId,
	                              bool bTeleportOwner = true);

	/** True when the room the observer stands in is loaded and visible. */
	UFUNCTION(BlueprintPure, Category = "MazeForge")
	bool IsCurrentRoomLoaded() const;

	/**
	 *  Fires when the room the observer arrived in is loaded AND visible.
	 *
	 *  This is what a fade-in binds to: the screen goes black when a door is taken and comes
	 *  back here, so the player never sees the empty world between the two mazes.
	 */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge")
	FMazeOnMazeReady OnMazeReady;

	/**
	 *  The shortest a transition is allowed to take, whatever the streaming says.
	 *
	 *  Readiness is not the only thing a fade is waiting for. The destination room is often
	 *  already loaded — every sublevel attached to the map arrives in PIE loaded, and a link
	 *  back into a maze that is still in memory needs nothing at all — and then the room is up
	 *  in the same frame as the switch. The screen blinks and the player is somewhere else,
	 *  which reads as a fault rather than a door.
	 *
	 *  So OnMazeReady waits for both: the room up, and this much time gone. Zero restores the
	 *  old behaviour of firing the moment the room is up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge",
		meta = (ClampMin = "0.0", UIMax = "3.0"))
	float MinimumTransitionSeconds = 0.5f;

	/**
	 *  How long to wait for that room before firing OnMazeReady anyway, with an error.
	 *
	 *  A safety catch, not a feature. Whatever holds a black screen on OnMazeReady has no other
	 *  way out, so a room that never comes up would hang the game with nothing on screen and
	 *  nothing in the log. Zero disables the catch, which is only sensible while hunting
	 *  exactly that fault.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge",
		meta = (ClampMin = "0.0", UIMax = "15.0"))
	float MazeReadyTimeoutSeconds = 5.0f;

	/** Called by the subsystem after each pool update. Not for game code. */
	void NotifyStreamingUpdated(const UMazeRoomPool& InPool, FName CurrentRoomId);

	/** A snapshot of the state for the rules. */
	FMazeStreamQuery BuildQuery(const UMazeWorldManifest* InManifest) const;

	/** Fixes up the camera bounds through reflection, to avoid a dependency on the game module. */
	void ApplyCameraBounds(const UMazeWorldManifest& InManifest) const;

	/**
	 *  Tells the camera that this frame is a cut, not a movement.
	 *
	 *  Called from the switch itself. A following camera cannot tell a teleport from a sprint,
	 *  and without this it travels the whole distance between two mazes on screen.
	 */
	void CutCamera() const;

protected:
	/**
	 *  Half of the visible camera frame in world units, computed on the player's plane.
	 *
	 *  Without this the camera frame rule would work off a constant and, at a different
	 *  FOV or resolution, would load something other than what is visible.
	 */
	static FVector2D ComputeCameraHalfExtentXZ(const APlayerCameraManager& CameraManager,
	                                           const FVector& PlayerLocation);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Set by SwitchToMaze, cleared when OnMazeReady fires. Nothing else broadcasts it. */
	bool bAwaitingMaze = false;

	/** World time of that switch, so the timeout above has something to measure from. */
	float AwaitingSinceSeconds = 0.0f;
};
