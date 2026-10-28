#include "Gameplay/MazeLiftComponent.h"

#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Gameplay/MazeProgressSubsystem.h"
#include "MazeForgeCore.h"
#include "Spawner/MazeObjectIdComponent.h"

UMazeLiftComponent::UMazeLiftComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UMazeLiftComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	const UMazeObjectIdComponent* Id = Owner ? Owner->FindComponentByClass<UMazeObjectIdComponent>() : nullptr;
	if (Id)
	{
		if (LiftId.IsNone())
		{
			LiftId = Id->LiftId;
		}
		if (Stops.Num() == 0)
		{
			Stops = Id->LiftStops;
		}
		if (StartStop < 0)
		{
			StartStop = Id->LiftStop;
		}
	}

	if (Stops.Num() == 0)
	{
		Stops.Add(0.0f);
	}

	// A Static root cannot be moved at all: say so, and make it Movable rather than stand still.
	USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	if (Root && Root->Mobility != EComponentMobility::Movable)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Lift '%s' (%s): the root component is not Movable — set Mobility = Movable in the Blueprint."),
			*LiftId.ToString(), *Owner->GetName());
		Root->SetMobility(EComponentMobility::Movable);
	}

	if (Stops.Num() < 2)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Lift '%s' (%s): one stop only, nowhere to ride. Place it with the Lift tool (two clicks) and Apply Changes."),
			*LiftId.ToString(), *GetNameSafe(Owner));
	}

	// The export spawns it at the lowest stop; every stop is measured from there. Once: a room
	// hidden and shown again plays BeginPlay again on the lift wherever it was left.
	if (!bBaseKnown)
	{
		BaseLocation = Owner ? Owner->GetActorLocation() - FVector(0.0, 0.0, Stops[0]) : FVector::ZeroVector;
		bBaseKnown = true;
	}
	bMoving = false;
	bReady = false;

	CurrentStop = FMath::Clamp(StartStop < 0 ? 0 : StartStop, 0, Stops.Num() - 1);

	// Where it was left, if its room has been loaded before in this session.
	int32 Saved = 0;
	const UMazeProgressSubsystem* Progress = UMazeProgressSubsystem::Get(this);
	if (Progress && Id && Progress->GetState(Id->PlacementId, Saved) && Stops.IsValidIndex(Saved))
	{
		CurrentStop = Saved;
	}

	TargetStop = CurrentStop;
	PlaceAt(Stops[CurrentStop]);

	if (UMazeLiftSubsystem* Registry = UMazeLiftSubsystem::Get(this))
	{
		Registry->Register(this);
	}
}

void UMazeLiftComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UMazeLiftSubsystem* Registry = UMazeLiftSubsystem::Get(this))
	{
		Registry->Unregister(this);
	}
	Super::EndPlay(Reason);
}

bool UMazeLiftComponent::MoveToStop(int32 Stop)
{
	return StartRide(Stop, /*bCalled*/ true);
}

bool UMazeLiftComponent::StartRide(int32 Stop, bool bCalled)
{
	if (!Stops.IsValidIndex(Stop))
	{
		return false;
	}
	if (bMoving && bIgnoreCallsWhileMoving)
	{
		return false;
	}
	if (!bMoving && Stop == CurrentStop)
	{
		// Already here: ready for whoever steps on. Said again, so a terminal can open its gate.
		if (bCalled)
		{
			UE_LOG(LogMazeForge, Log, TEXT("Lift '%s': called to stop %d — already there, ready to ride."),
				*LiftId.ToString(), Stop);
			bReady = true;
			BoardTime = 0.0f;
			SetComponentTickEnabled(bRideWhenBoarded);
		}
		OnArrived.Broadcast(CurrentStop);
		return true;
	}

	UE_LOG(LogMazeForge, Log, TEXT("Lift '%s': to stop %d (%.0f cm)."), *LiftId.ToString(), Stop, Stops[Stop]);
	TargetStop = Stop;
	bMoving = true;
	bReady = false;
	bReadyOnArrival = bCalled;
	Crushed.Reset();
	SetComponentTickEnabled(true);
	OnDeparted.Broadcast(TargetStop);
	return true;
}

bool UMazeLiftComponent::MoveToOtherStop()
{
	if (Stops.Num() < 2)
	{
		return false;
	}
	const int32 From = bMoving ? TargetStop : CurrentStop;
	return StartRide((From + 1) % Stops.Num(), /*bCalled*/ false);
}

void UMazeLiftComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bMoving || !Stops.IsValidIndex(TargetStop))
	{
		// Called and waiting: a player who steps on and stays rides it to the other stop.
		if (!bReady || !bRideWhenBoarded || Stops.Num() < 2)
		{
			SetComponentTickEnabled(false);
			return;
		}

		if (!HasRider())
		{
			BoardTime = 0.0f;
		}
		else
		{
			BoardTime += DeltaTime;
			if (BoardTime >= BoardDelay)
			{
				BoardTime = 0.0f;
				UE_LOG(LogMazeForge, Log, TEXT("Lift '%s': boarded, riding."), *LiftId.ToString());
				MoveToOtherStop();
			}
		}
		return;
	}

	const float Target = Stops[TargetStop];
	const float NewHeight = FMath::FInterpConstantTo(Height, Target, DeltaTime, Speed);
	if (bCrushes && NewHeight < Height)
	{
		CrushBelow(Height - NewHeight);
	}
	PlaceAt(NewHeight);

	if (FMath::IsNearlyEqual(Height, Target, 0.01f))
	{
		bMoving = false;
		CurrentStop = TargetStop;
		// Brought here by a call: ready for a rider. Ridden here: it waits for the next call.
		bReady = bReadyOnArrival;
		BoardTime = 0.0f;
		SetComponentTickEnabled(bReady && bRideWhenBoarded);

		if (UMazeProgressSubsystem* Progress = UMazeProgressSubsystem::Get(this))
		{
			Progress->SetState(UMazeObjectIdComponent::GetPlacementId(GetOwner()), CurrentStop);
		}
		OnArrived.Broadcast(CurrentStop);
	}
}

void UMazeLiftComponent::PlaceAt(float NewHeight)
{
	Height = NewHeight;
	if (AActor* Owner = GetOwner())
	{
		Owner->SetActorLocation(BaseLocation + FVector(0.0, 0.0, Height));
	}
}

bool UMazeLiftComponent::HasRider() const
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return false;
	}

	const FBox Bounds = Owner->GetComponentsBoundingBox(false);
	if (!Bounds.IsValid)
	{
		return false;
	}

	// A thin slab on top of it; who is standing there and stands on it, not beside it.
	const FVector Half = Bounds.GetExtent();
	const FVector Extent(FMath::Max(1.0, Half.X), FMath::Max(1.0, Half.Y), 30.0);
	const FVector Center(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Max.Z + Extent.Z);

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MazeLiftRider), false, Owner);
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeBox(Extent), Params);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		const ACharacter* Character = Cast<ACharacter>(Overlap.GetActor());
		const UPrimitiveComponent* Base = Character ? Character->GetMovementBase() : nullptr;
		if (Character && Character->IsPlayerControlled() && Base && Base->GetOwner() == Owner)
		{
			return true;
		}
	}
	return false;
}

void UMazeLiftComponent::CrushBelow(float Drop)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	// What it is made of that collides: decoration hanging below does not crush anyone.
	const FBox Bounds = Owner->GetComponentsBoundingBox(false);
	if (!Bounds.IsValid)
	{
		return;
	}

	// The slab of space it is about to move through, and the head-touching margin under that.
	const FVector Half = Bounds.GetExtent();
	const FVector Extent(FMath::Max(1.0, Half.X - CrushInset), FMath::Max(1.0, Half.Y - CrushInset),
		0.5 * (Drop + CrushReach));
	const FVector Center(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z - Extent.Z);

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MazeLiftCrush), false, Owner);
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeBox(Extent), Params);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		APawn* Victim = Cast<APawn>(Overlap.GetActor());
		if (!Victim || Victim->GetAttachParentActor() == Owner || Crushed.Contains(Victim))
		{
			continue;
		}

		Crushed.Add(Victim);
		UE_LOG(LogMazeForge, Log, TEXT("Lift '%s': crushed %s."), *LiftId.ToString(), *Victim->GetName());
		OnCrushed.Broadcast(Victim);
		if (CrushDamage > 0.0f)
		{
			UGameplayStatics::ApplyDamage(Victim, CrushDamage, nullptr, Owner, UDamageType::StaticClass());
		}
	}
}

// ---------------------------------------------------------------- registry

void UMazeLiftSubsystem::Register(UMazeLiftComponent* Lift)
{
	if (!Lift || Lift->LiftId.IsNone())
	{
		return;
	}

	const TWeakObjectPtr<UMazeLiftComponent>* Existing = Lifts.Find(Lift->LiftId);
	if (Existing && Existing->IsValid() && Existing->Get() != Lift)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Lift: two lifts are called '%s'; terminals will call the one loaded last. Give "
			     "them different Lift Ids."), *Lift->LiftId.ToString());
	}
	Lifts.Add(Lift->LiftId, Lift);
}

void UMazeLiftSubsystem::Unregister(UMazeLiftComponent* Lift)
{
	if (!Lift)
	{
		return;
	}
	const TWeakObjectPtr<UMazeLiftComponent>* Existing = Lifts.Find(Lift->LiftId);
	if (Existing && Existing->Get() == Lift)
	{
		Lifts.Remove(Lift->LiftId);
	}
}

UMazeLiftComponent* UMazeLiftSubsystem::FindLift(FName LiftId) const
{
	const TWeakObjectPtr<UMazeLiftComponent>* Found = Lifts.Find(LiftId);
	return Found ? Found->Get() : nullptr;
}

UMazeLiftSubsystem* UMazeLiftSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UMazeLiftSubsystem>() : nullptr;
}
