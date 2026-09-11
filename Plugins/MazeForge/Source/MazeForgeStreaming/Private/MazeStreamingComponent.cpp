#include "MazeStreamingComponent.h"

#include "Assets/MazeWorldGraph.h"
#include "Assets/MazeWorldManifest.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/Actor.h"
#include "MazeRoomPool.h"
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

	SwitchToMazeAtTransition(Link->ToMaze, Link->ToId);
	return true;
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

	// Moved here, before the subsystem's next tick, and deliberately. See the header: the pool
	// scores rooms by where the observer is, so the move has to happen first or there is
	// nothing for the new maze to load around.
	if (bTeleportOwner)
	{
		if (AActor* Owner = GetOwner())
		{
			Owner->SetActorLocation(EntryLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	UE_LOG(LogMazeForge, Log, TEXT("Streaming: switching to manifest %s, entry at %s."),
		*Manifest.ToString(), *EntryLocation.ToString());
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

	// Visible and not merely loaded: a level that is loaded but not yet added to the world is
	// geometry the player would fall through, and fading back in there is worse than waiting.
	if (Streaming && Streaming->IsLevelLoaded() && Streaming->IsLevelVisible())
	{
		bAwaitingMaze = false;

		UE_LOG(LogMazeForge, Log, TEXT("Streaming: room %s is up — the maze is ready."),
			*CurrentRoomId.ToString());

		OnMazeReady.Broadcast();
	}
}

void UMazeStreamingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
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
