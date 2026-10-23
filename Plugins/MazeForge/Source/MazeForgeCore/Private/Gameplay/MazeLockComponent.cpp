#include "Gameplay/MazeLockComponent.h"

#include "GameFramework/Actor.h"
#include "Gameplay/MazeKeyringComponent.h"
#include "Gameplay/MazeProgressSubsystem.h"
#include "Spawner/MazeObjectIdComponent.h"

UMazeLockComponent::UMazeLockComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMazeLockComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	const UMazeObjectIdComponent* Id = Owner ? Owner->FindComponentByClass<UMazeObjectIdComponent>() : nullptr;
	if (LockId.IsNone() && Id)
	{
		LockId = Id->LockId;
	}

	const UMazeProgressSubsystem* Progress = UMazeProgressSubsystem::Get(this);
	if (Id && Progress && Progress->IsUsed(Id->PlacementId))
	{
		Open(nullptr, /*bRestored*/ true);
		return;
	}

	if (Owner && bUnlockOnTouch)
	{
		Owner->OnActorBeginOverlap.AddDynamic(this, &UMazeLockComponent::HandleOverlap);
	}
}

void UMazeLockComponent::HandleOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	if (bLocked && UMazeKeyringComponent::Find(OtherActor))
	{
		TryUnlock(OtherActor);
	}
}

bool UMazeLockComponent::TryUnlock(AActor* Opener)
{
	if (!bLocked)
	{
		return true;
	}

	UMazeKeyringComponent* Keyring = UMazeKeyringComponent::Find(Opener);
	if (!Keyring || !Keyring->HasKey(LockId))
	{
		OnRefused.Broadcast(Opener);
		return false;
	}

	if (bConsumeKey)
	{
		Keyring->RemoveKey(LockId);
	}

	if (UMazeProgressSubsystem* Progress = UMazeProgressSubsystem::Get(this))
	{
		Progress->MarkUsed(UMazeObjectIdComponent::GetPlacementId(GetOwner()));
	}

	Open(Opener, false);
	return true;
}

void UMazeLockComponent::Open(AActor* Opener, bool bRestored)
{
	bLocked = false;
	OnUnlocked.Broadcast(Opener, bRestored);
}
