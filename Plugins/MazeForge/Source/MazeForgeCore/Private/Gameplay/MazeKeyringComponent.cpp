#include "Gameplay/MazeKeyringComponent.h"

#include "GameFramework/Actor.h"

UMazeKeyringComponent::UMazeKeyringComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UMazeKeyringComponent::AddKey(FName LockId)
{
	if (LockId.IsNone() || Keys.Contains(LockId))
	{
		return false;
	}

	Keys.Add(LockId);
	OnKeyAdded.Broadcast(LockId);
	return true;
}

bool UMazeKeyringComponent::HasKey(FName LockId) const
{
	return !LockId.IsNone() && Keys.Contains(LockId);
}

bool UMazeKeyringComponent::RemoveKey(FName LockId)
{
	return Keys.Remove(LockId) > 0;
}

UMazeKeyringComponent* UMazeKeyringComponent::Find(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UMazeKeyringComponent>() : nullptr;
}
