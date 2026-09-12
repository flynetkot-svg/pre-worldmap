#include "MazeStreamingComponent.h"

#include "Assets/MazeWorldGraph.h"
#include "Assets/MazeWorldManifest.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "MazeRoomPool.h"
#include "Spawner/MazeObjectIdComponent.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "MazeForgeCore.h"
#include "MazeStreamingSubsystem.h"

UMazeStreamingComponent::UMazeStreamingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMazeStreamingComponent::BeginPlay()
{
	Super::BeginPlay();

	// Named once, at the start, and it is the line that was missing the evening a maze silently
	// did not appear: the component was pointing at the manifest of another maze, and nothing
	// anywhere in the log said which manifest was being read. ToString does not load it.
	const FString ManifestPath = Manifest.IsNull()
		? FString(TEXT("(none — nothing will stream)"))
		: Manifest.ToString();

	UE_LOG(LogMazeForge, Log, TEXT("Streaming: %s reads manifest %s"),
		*GetNameSafe(GetOwner()), *ManifestPath);

	if (UWorld* World = GetWorld())
	{
		if (UMazeStreamingSubsystem* Subsystem = World->GetSubsystem<UMazeStreamingSubsystem>())
		{
			Subsystem->RegisterObserver(this);
		}
	}
}

bool UMazeStreamingComponent::TakeTransitionFor(AActor* Traveller, AActor* Gate)
{
	if (!Gate)
	{
		UE_LOG(LogMazeForge, Error,
			TEXT("Take Transition For was given no gate. Wire Self into the Gate pin."));
		return false;
	}

	const int32 PlacementId = UMazeObjectIdComponent::GetPlacementId(Gate);
	if (PlacementId == 0)
	{
		// Either this actor was placed by hand rather than built by the export, or its library
		// type is not a Gate. Both are configuration, and both look identical from the game.
		UE_LOG(LogMazeForge, Error,
			TEXT("%s carries no placement id, so it is not a gate the export built. Check that "
			     "its library type has Transition Role = Gate, and that the maze has been built "
			     "since the point was placed."),
			*GetNameSafe(Gate));
		return false;
	}

	UMazeStreamingComponent* Component = Traveller
		? Traveller->FindComponentByClass<UMazeStreamingComponent>()
		: nullptr;

	if (!Component)
	{
		// The silence this whole function exists to end: whatever walked in is not the player,
		// and the three-node version of this had no way of saying so.
		UE_LOG(LogMazeForge, Warning,
			TEXT("%s walked into gate %d but has no MazeForge Streaming component, so it is not "
			     "the player and nothing happens. Set the volume to overlap Pawn only, or check "
			     "that the component really is on the character."),
			*GetNameSafe(Traveller), PlacementId);
		return false;
	}

	return Component->TakeTransition(PlacementId);
}

bool UMazeStreamingComponent::TakeTransition(const int32 PlacementId)
{
	UMazeWorldGraph* Graph = WorldGraph.LoadSynchronous();
	if (!Graph)
	{
		UE_LOG(LogMazeForge, Error,
			TEXT("Streaming: %s has no World Graph — the field is empty or points at an asset "
			     "that is gone (%s). A door has nothing to ask."),
			*GetNameSafe(GetOwner()), *WorldGraph.ToString());
		return false;
	}

	const UMazeWorldManifest* Here = Manifest.Get();
	if (!Here)
	{
		UE_LOG(LogMazeForge, Error,
			TEXT("Streaming: %s does not know which maze it is in, so a door in it cannot be "
			     "looked up."),
			*GetNameSafe(GetOwner()));
		return false;
	}

	const FMazeWorldLink* Link = Graph->FindLinkFrom(Here, PlacementId);
	if (!Link)
	{
		// The normal way a door goes dead, and worth saying in full: the point was deleted and
		// drawn again, so it is a different point with a different number, and the link in the
		// graph still names the one that no longer exists.
		UE_LOG(LogMazeForge, Warning,
			TEXT("Streaming: gate %d of %s leads nowhere — %s has no link starting there. "
			     "Draw it on the world map; if it was drawn, check whether the point was "
			     "re-placed and took a new number."),
			PlacementId, *Here->GetName(), *Graph->GetName());
		return false;
	}

	// Announced now, acted on in a moment. All of this used to happen inside the call, which
	// meant the world moved while the screen was still transparent.
	BeginTransition(Link->ToMaze, Link->ToId);
	return true;
}

void UMazeStreamingComponent::BeginTransition(
	const TSoftObjectPtr<UMazeWorldManifest>& NewManifest, const int32 TransitionId)
{
	// A door firing twice is the normal case, not the odd one: a trigger volume sees the
	// capsule and the mesh, and two gates can overlap. Without this the second call arrives
	// mid-fade and sends the player straight back out through the return link.
	if (bTransitionPending || bAwaitingMaze)
	{
		UE_LOG(LogMazeForge, Log,
			TEXT("Streaming: a transition is already under way; this one is ignored. Normal "
			     "when a trigger volume catches both the capsule and the mesh."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bTransitionPending = true;
	PendingMaze = NewManifest;
	PendingTransitionId = TransitionId;
	TransitionSinceSeconds = World->GetTimeSeconds();

	OnTransitionStarted.Broadcast();
	FadeOut();

	UE_LOG(LogMazeForge, Log,
		TEXT("Streaming: transition started; moving in %.2f s."), TransitionFadeOutSeconds);

	if (TransitionFadeOutSeconds <= 0.0f)
	{
		CommitPendingTransition();
		return;
	}

	World->GetTimerManager().SetTimer(TransitionTimer, this,
		&UMazeStreamingComponent::CommitPendingTransition, TransitionFadeOutSeconds, false);
}

void UMazeStreamingComponent::CommitPendingTransition()
{
	if (!bTransitionPending)
	{
		return;
	}

	bTransitionPending = false;

	// Saved across the switch and put back: SwitchToMazeAtLocation stamps this with the moment
	// of the move, and the minimum and the timeout are both about how long the player sits in
	// front of a black screen — which starts at the door, not at the move.
	const float StartedAt = TransitionSinceSeconds;

	SwitchToMazeAtTransition(PendingMaze, PendingTransitionId);

	TransitionSinceSeconds = StartedAt;

	PendingMaze.Reset();
	PendingTransitionId = 0;
}

APlayerCameraManager* UMazeStreamingComponent::FindCameraManager() const
{
	const AActor* Owner = GetOwner();
	const APlayerController* Controller =
		Owner ? Owner->GetInstigatorController<APlayerController>() : nullptr;

	return Controller ? Controller->PlayerCameraManager : nullptr;
}

void UMazeStreamingComponent::FadeOut() const
{
	if (!bHandleTransitionFade)
	{
		return;
	}

	if (APlayerCameraManager* CameraManager = FindCameraManager())
	{
		// Held when finished, because the world is not ready when the fade ends — that is the
		// whole point of it. FadeIn releases it, and if the room never comes up the timeout in
		// NotifyStreamingUpdated releases it anyway rather than leaving a dead screen.
		CameraManager->StartCameraFade(0.0f, 1.0f, TransitionFadeOutSeconds,
			TransitionFadeColor, /*bFadeAudio*/ true, /*bHoldWhenFinished*/ true);
	}
}

void UMazeStreamingComponent::FadeIn() const
{
	if (!bHandleTransitionFade)
	{
		return;
	}

	if (APlayerCameraManager* CameraManager = FindCameraManager())
	{
		CameraManager->StartCameraFade(1.0f, 0.0f, TransitionFadeInSeconds,
			TransitionFadeColor, /*bFadeAudio*/ true, /*bHoldWhenFinished*/ false);
	}
}

void UMazeStreamingComponent::SwitchToMazeAtTransition(
	TSoftObjectPtr<UMazeWorldManifest> NewManifest, const int32 TransitionId,
	const bool bTeleportOwner)
{
	UMazeWorldManifest* Destination = NewManifest.LoadSynchronous();
	if (!Destination)
	{
		UE_LOG(LogMazeForge, Error,
			TEXT("Streaming: could not load manifest %s. Nothing changed."),
			*NewManifest.ToString());
		return;
	}

	const FMazeTransitionPoint* Point = Destination->FindTransition(TransitionId);
	if (!Point)
	{
		FString Available;
		for (const FMazeTransitionPoint& Known : Destination->Transitions)
		{
			Available += Available.IsEmpty() ? TEXT("") : TEXT(", ");
			Available += FString::Printf(TEXT("%d (%s)"), Known.Id,
				Known.Role == EMazeTransitionRole::Gate ? TEXT("gate") : TEXT("entry"));
		}

		// Naming what IS there, because the fault is nearly always a point that was re-placed
		// and took a new number, and the list answers that in one glance.
		UE_LOG(LogMazeForge, Error,
			TEXT("Streaming: manifest %s has no transition %d. It has: %s. Nothing changed."),
			*Destination->GetName(), TransitionId,
			Available.IsEmpty() ? TEXT("none at all") : *Available);
		return;
	}

	if (bTeleportOwner)
	{
		if (AActor* Owner = GetOwner())
		{
			Owner->SetActorRotation(Point->Rotation);
		}
	}

	SwitchToMazeAtLocation(NewManifest, Point->Location, bTeleportOwner);
}

void UMazeStreamingComponent::SwitchToMazeAtLocation(TSoftObjectPtr<UMazeWorldManifest> NewManifest,
                                                     FVector EntryLocation, bool bTeleportOwner)
{
	if (NewManifest.IsNull())
	{
		UE_LOG(LogMazeForge, Error,
			TEXT("Streaming: Switch To Maze was given no manifest. Nothing changed."));
		return;
	}

	Manifest = NewManifest;
	bAwaitingMaze = true;
	TransitionSinceSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	// Moved here, before the subsystem's next tick, and deliberately. See the header: the pool
	// scores rooms by where the observer is, so the move has to happen first or there is
	// nothing for the new maze to load around.
	if (bTeleportOwner)
	{
		if (AActor* Owner = GetOwner())
		{
			Owner->SetActorLocation(EntryLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}

		CutCamera();
	}

	UE_LOG(LogMazeForge, Log, TEXT("Streaming: switching to manifest %s, entry at %s."),
		*Manifest.ToString(), *EntryLocation.ToString());
}

void UMazeStreamingComponent::CutCamera() const
{
	// A side-view camera follows the player by interpolating towards him every frame, which is
	// right for walking and wrong for a door: a jump of tens of thousands of units reads to it
	// as a very fast run, and it sets off across the world in plain sight. The engine has one
	// word for "this is a cut, do not interpolate", and it has to be said in the same frame as
	// the teleport — hence here, next to the move, rather than left to the game to remember.
	if (APlayerCameraManager* CameraManager = FindCameraManager())
	{
		CameraManager->SetGameCameraCutThisFrame();
	}
}

bool UMazeStreamingComponent::IsCurrentRoomLoaded() const
{
	const UWorld* World = GetWorld();
	const UMazeStreamingSubsystem* Subsystem = World
		? World->GetSubsystem<UMazeStreamingSubsystem>() : nullptr;
	const UMazeRoomPool* CurrentPool = Subsystem ? Subsystem->GetPool() : nullptr;
	const UMazeWorldManifest* CurrentManifest = Manifest.Get();

	if (!CurrentPool || !CurrentManifest)
	{
		return false;
	}

	const AActor* Owner = GetOwner();
	const FName RoomId = Owner
		? CurrentManifest->FindRoomAt(Owner->GetActorLocation())
		: FName();

	const FMazeRoomRuntime* State = CurrentPool->GetRooms().Find(RoomId);
	const ULevelStreaming* Streaming = State ? State->Streaming.Get() : nullptr;

	return Streaming && Streaming->IsLevelLoaded() && Streaming->IsLevelVisible();
}

void UMazeStreamingComponent::NotifyStreamingUpdated(const UMazeRoomPool& InPool,
                                                     const FName CurrentRoomId)
{
	if (!bAwaitingMaze)
	{
		return;
	}

	const FMazeRoomRuntime* State = InPool.GetRooms().Find(CurrentRoomId);
	const ULevelStreaming* Streaming = State ? State->Streaming.Get() : nullptr;

	const UWorld* World = GetWorld();
	const float Waited = World ? World->GetTimeSeconds() - TransitionSinceSeconds : 0.0f;

	// Visible and not merely loaded: a level that is loaded but not yet added to the world is
	// geometry the player would fall through, and fading back in there is worse than waiting.
	if (Streaming && Streaming->IsLevelLoaded() && Streaming->IsLevelVisible())
	{
		// Held back until the transition has lasted at least its minimum. See the header: the
		// room is very often up in the same frame as the switch, and a fade that comes straight
		// back reads as a glitch. The package name is logged because it is the one thing that
		// separates "the destination was already in memory" from "the level we are looking at
		// is the one we just left, still reporting visible while its unload catches up".
		if (Waited < MinimumTransitionSeconds)
		{
			return;
		}

		bAwaitingMaze = false;

		UE_LOG(LogMazeForge, Log,
			TEXT("Streaming: room %s (%s) is up — the maze is ready, %.2f s after the door."),
			*CurrentRoomId.ToString(), *Streaming->GetWorldAssetPackageName(), Waited);

		FadeIn();
		OnMazeReady.Broadcast();
		return;
	}

	// The room may never come up: a manifest naming levels that are not attached, a room id the
	// player is standing outside of, a load that failed. The game is holding a black screen on
	// this event, so never arriving means never coming back — a hang with no message, which is
	// the worst failure this plugin can produce. Better a visibly broken arrival than a dead
	// screen, and an error line that says which room did not come up.
	if (MazeReadyTimeoutSeconds > 0.0f && Waited > MazeReadyTimeoutSeconds)
	{
		bAwaitingMaze = false;

		UE_LOG(LogMazeForge, Error,
			TEXT("Streaming: room %s did not come up within %.1f s of the switch to %s. "
			     "Bringing the screen back anyway so the game is not left staring at black. "
			     "Check that the room exists in that manifest and that its level is attached "
			     "to the map."),
			*CurrentRoomId.ToString(), MazeReadyTimeoutSeconds, *Manifest.ToString());

		FadeIn();
		OnMazeReady.Broadcast();
	}
}

void UMazeStreamingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		// A transition scheduled a moment before the level ends has nothing left to move to.
		World->GetTimerManager().ClearTimer(TransitionTimer);
		bTransitionPending = false;

		if (UMazeStreamingSubsystem* Subsystem = World->GetSubsystem<UMazeStreamingSubsystem>())
		{
			Subsystem->UnregisterObserver(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

FVector2D UMazeStreamingComponent::ComputeCameraHalfExtentXZ(const APlayerCameraManager& CameraManager,
                                                             const FVector& PlayerLocation)
{
	const FMinimalViewInfo& View = CameraManager.GetCameraCacheView();

	// AspectRatio is not always filled in: on an unconstrained camera it is zero.
	const double Aspect = (View.AspectRatio > KINDA_SMALL_NUMBER)
		? static_cast<double>(View.AspectRatio)
		: 16.0 / 9.0;

	double HalfX = 0.0;

	if (View.ProjectionMode == ECameraProjectionMode::Orthographic)
	{
		HalfX = View.OrthoWidth * 0.5;
	}
	else
	{
		// We measure the frame on the player's plane: the camera is offset along Y, looking there.
		const double DistanceToPlane = FMath::Abs(View.Location.Y - PlayerLocation.Y);
		if (DistanceToPlane < 1.0)
		{
			return FVector2D(1920.0, 1080.0);
		}

		// In UE the FOV is the whole horizontal angle.
		HalfX = DistanceToPlane * FMath::Tan(FMath::DegreesToRadians(View.FOV * 0.5f));
	}

	if (HalfX < 1.0)
	{
		return FVector2D(1920.0, 1080.0);
	}

	return FVector2D(HalfX, HalfX / Aspect);
}

FMazeStreamQuery UMazeStreamingComponent::BuildQuery(const UMazeWorldManifest* InManifest) const
{
	FMazeStreamQuery Query;

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return Query;
	}

	Query.PlayerLocation = Owner->GetActorLocation();
	Query.PlayerVelocity = Owner->GetVelocity();
	Query.CameraLocation = Query.PlayerLocation;

	if (const ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Query.GravityZ = Movement->GetGravityZ();

			switch (Movement->MovementMode)
			{
			case MOVE_Falling:  Query.MotionMode = EMazeMotionMode::Falling;  break;
			case MOVE_Flying:   Query.MotionMode = EMazeMotionMode::Flying;   break;
			case MOVE_Custom:   Query.MotionMode = EMazeMotionMode::Climbing; break;
			default:            Query.MotionMode = EMazeMotionMode::Walking;  break;
			}
		}
	}

	// The camera is offset from the player and has its own frame — the rules look at it.
	if (const APlayerController* Controller = Owner->GetInstigatorController<APlayerController>())
	{
		if (const APlayerCameraManager* CameraManager = Controller->PlayerCameraManager)
		{
			Query.CameraLocation = CameraManager->GetCameraLocation();
			Query.CameraHalfExtentXZ = ComputeCameraHalfExtentXZ(*CameraManager, Query.PlayerLocation);
		}
	}

	if (InManifest)
	{
		Query.CurrentRoomId = InManifest->FindRoomAt(Query.PlayerLocation);
	}

	return Query;
}

void UMazeStreamingComponent::ApplyCameraBounds(const UMazeWorldManifest& InManifest) const
{
	if (!bDriveCameraBounds || !InManifest.WorldBounds.IsValid)
	{
		return;
	}

	const AActor* Owner = GetOwner();
	const APlayerController* Controller = Owner ? Owner->GetInstigatorController<APlayerController>() : nullptr;
	APlayerCameraManager* CameraManager = Controller ? Controller->PlayerCameraManager : nullptr;
	if (!CameraManager)
	{
		return;
	}

	// The properties live in the project's game module, which the streaming module must not know.
	// Reflection is the price of that independence: with another camera nothing simply happens.
	UClass* CameraClass = CameraManager->GetClass();

	if (FFloatProperty* MinProp = FindFProperty<FFloatProperty>(CameraClass, TEXT("CameraXMinBounds")))
	{
		MinProp->SetPropertyValue_InContainer(CameraManager, static_cast<float>(InManifest.WorldBounds.Min.X));
	}

	if (FFloatProperty* MaxProp = FindFProperty<FFloatProperty>(CameraClass, TEXT("CameraXMaxBounds")))
	{
		MaxProp->SetPropertyValue_InContainer(CameraManager, static_cast<float>(InManifest.WorldBounds.Max.X));
		UE_LOG(LogMazeForge, Log, TEXT("Camera bounds set from the manifest: X %.0f..%.0f"),
			InManifest.WorldBounds.Min.X, InManifest.WorldBounds.Max.X);
	}
}
