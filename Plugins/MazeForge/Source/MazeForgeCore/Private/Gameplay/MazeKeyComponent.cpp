#include "Gameplay/MazeKeyComponent.h"

#include "GameFramework/Actor.h"
#include "Gameplay/MazeKeyringComponent.h"
#include "Gameplay/MazeProgressSubsystem.h"
#include "Spawner/MazeObjectIdComponent.h"

UMazeKeyComponent::UMazeKeyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMazeKeyComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	const UMazeObjectIdComponent* Id = Owner ? Owner->FindComponentByClass<UMazeObjectIdComponent>() : nullptr;
	if (LockId.IsNone() && Id)
	{
		LockId = Id->LockId;
	}

	// Its room streamed in again after the key was taken: it is not there any more.
	const UMazeProgressSubsystem* Progress = UMazeProgressSubsystem::Get(this);
	if (Owner && Id && Progress && Progress->IsUsed(Id->PlacementId))
	{
		bTaken = true;
		Owner->Destroy();
		return;
	}

	if (Owner && bPickUpOnTouch)
	{
		Owner->OnActorBeginOverlap.AddDynamic(this, &UMazeKeyComponent::HandleOverlap);
	}
}

void UMazeKeyComponent::HandleOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	TryPickUp(OtherActor);
}

bool UMazeKeyComponent::TryPickUp(AActor* Collector)
{
	UMazeKeyringComponent* Keyring = UMazeKeyringComponent::Find(Collector);
	if (bTaken || !Keyring)
	{
		return false;
	}

	bTaken = true;
	Keyring->AddKey(LockId);

	if (UMazeProgressSubsystem* Progress = UMazeProgressSubsystem::Get(this))
	{
		Progress->MarkUsed(UMazeObjectIdComponent::GetPlacementId(GetOwner()));
	}

	OnPickedUp.Broadcast(Collector);

	if (bDestroyOnPickUp && GetOwner())
	{
		GetOwner()->Destroy();
	}
	return true;
}
