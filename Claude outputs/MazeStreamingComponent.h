#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
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
 *  Fires the instant a door is taken, before anything moves.
 *
 *  The pair with OnMazeReady is the whole transition: hide the world here, show it again there.
 *  Nothing has happened yet when this fires — that is the point of it firing first.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMazeOnTransitionStarted);

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
	 *  Fires the moment a door is taken, before the observer has moved.
	 *
	 *  Bind a fade-out, a cutscene or a summary screen here. The move and the manifest switch
	 *  wait TransitionFadeOutSeconds after this, so whatever hides the world has time to do it
	 *  before there is anything to hide.
	 */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge")
	FMazeOnTransitionStarted OnTransitionStarted;

	/**
	 *  Fires when the room the observer arrived in is loaded AND visible.
	 *
	 *  The other half of the pair: the world is real again, show it.
	 */
	UPROPERTY(BlueprintAssignable, Category = "MazeForge")
	FMazeOnMazeReady OnMazeReady;

	/**
	 *  Do the fade in and out here rather than leaving it to the game.
	 *
	 *  On by default, and the default for a reason. The two halves of a fade have to agree
	 *  about colour, about alpha and about which one holds — and when they are two nodes in two
	 *  different blueprints, nothing checks that they still do. A cleared checkbox in a third
	 *  asset is enough to make a transition blink, and neither the compiler nor the log says a
	 *  word about it.
	 *
	 *  Turn it off to present the transition yourself. The events still fire, and the ordering
	 *  and the timing stay here where they belong — only the look becomes the game's business.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Transition")
	bool bHandleTransitionFade = true;

	/**
	 *  How long the world stays visible after a door is taken, before the observer moves.
	 *
	 *  This is the gap the fade-out lives in, and it exists because the old order was wrong:
	 *  the teleport happened inside the call and the fade only started, so for a quarter of a
	 *  second the player watched the next maze flick past and the camera set off after him.
	 *  Nothing moves until this has elapsed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Transition",
		meta = (ClampMin = "0.0", UIMax = "2.0"))
	float TransitionFadeOutSeconds = 0.25f;

	/** How long coming back takes, once the new room is up. Appearance only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Transition",
		meta = (ClampMin = "0.0", UIMax = "2.0", EditCondition = "bHandleTransitionFade"))
	float TransitionFadeInSeconds = 0.3f;

	/** What the screen fades to. Appearance only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Transition",
		meta = (EditCondition = "bHandleTransitionFade"))
	FLinearColor TransitionFadeColor = FLinearColor::Black;

	/**
	 *  The shortest a whole transition is allowed to take, measured from the door.
	 *
	 *  Readiness is not the only thing to wait for. The destination room is often already
	 *  loaded — every sublevel attached to the map arrives in PIE loaded, and a link back into
	 *  a maze still in memory needs nothing at all — and then it is up in the same frame as the
	 *  switch. Without a floor under it the screen blinks and the player is somewhere else,
	 *  which reads as a fault rather than as a door.
	 *
	 *  Zero lets a transition be as fast as the streaming allows.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MazeForge|Transition",
		meta = (ClampMin = "0.0", UIMax = "3.0"))
	float MinimumTransitionSeconds = 0.6f;

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

	/** The camera manager of whoever owns this component, or null off a player. */
	APlayerCameraManager* FindCameraManager() const;

protected:
	/** Announces the transition, starts the fade, and schedules the switch. */
	void BeginTransition(const TSoftObjectPtr<UMazeWorldManifest>& NewManifest, int32 TransitionId);

	/** What the timer set by BeginTransition runs: the move itself, behind a black screen. */
	void CommitPendingTransition();

	/** Fades the screen out or in, when bHandleTransitionFade says that is ours to do. */
	void FadeOut() const;
	void FadeIn() const;

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

	/**
	 *  World time the transition began — the door, not the switch.
	 *
	 *  Both the minimum and the timeout measure from here, so they mean what a designer reads
	 *  them to mean: how long the player is looking at a black screen, start to finish.
	 */
	float TransitionSinceSeconds = 0.0f;

	/** True between the door and the switch, while the screen is going dark. */
	bool bTransitionPending = false;

	/** Where that pending transition is going. Only meaningful while bTransitionPending. */
	TSoftObjectPtr<UMazeWorldManifest> PendingMaze;
	int32 PendingTransitionId = 0;

	FTimerHandle TransitionTimer;
};
