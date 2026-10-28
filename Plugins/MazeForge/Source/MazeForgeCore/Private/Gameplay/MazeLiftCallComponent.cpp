#include "Gameplay/MazeLiftCallComponent.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/MazeLiftComponent.h"
#include "MazeForgeCore.h"
#include "Spawner/MazeObjectIdComponent.h"

UMazeLiftCallComponent::UMazeLiftCallComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMazeLiftCallComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (const UMazeObjectIdComponent* Id = Owner ? Owner->FindComponentByClass<UMazeObjectIdComponent>() : nullptr)
	{
		if (LiftId.IsNone())
		{
			LiftId = Id->LiftId;
		}
		if (Stop < 0)
		{
			Stop = Id->LiftStop;
		}
	}

	if (Owner && bCallOnTouch)
	{
		Owner->OnActorBeginOverlap.AddUniqueDynamic(this, &UMazeLiftCallComponent::HandleOverlap);
	}
}

void UMazeLiftCallComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	// A room hidden by streaming and shown again plays BeginPlay a second time on the same
	// actor: without this the overlap would be bound twice.
	if (AActor* Owner = GetOwner())
	{
		Owner->OnActorBeginOverlap.RemoveDynamic(this, &UMazeLiftCallComponent::HandleOverlap);
	}
	Super::EndPlay(Reason);
}

void UMazeLiftCallComponent::HandleOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	if (Cast<APawn>(OtherActor))
	{
		Call();
	}
}

bool UMazeLiftCallComponent::Call()
{
	const UMazeLiftSubsystem* Registry = UMazeLiftSubsystem::Get(this);
	UMazeLiftComponent* Lift = Registry ? Registry->FindLift(LiftId) : nullptr;

	if (!Lift)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Lift call %s: no lift '%s' is loaded%s."), *GetNameSafe(GetOwner()), *LiftId.ToString(),
			LiftId.IsNone()
				? TEXT(" — the export bound this terminal to nothing; see its warning, then Apply Changes")
				: TEXT(" — its room is streamed out, or it has another Lift Id"));
		OnRefused.Broadcast();
		return false;
	}

	if (!Lift->MoveToStop(Stop))
	{
		UE_LOG(LogMazeForge, Log, TEXT("Lift call %s: lift '%s' refused stop %d (riding, or no such stop)."),
			*GetNameSafe(GetOwner()), *LiftId.ToString(), Stop);
		OnRefused.Broadcast();
		return false;
	}

	OnCalled.Broadcast(Stop);
	return true;
}
