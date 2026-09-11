#include "Spawner/MazeObjectIdComponent.h"

#include "GameFramework/Actor.h"

UMazeObjectIdComponent::UMazeObjectIdComponent()
{
	// It holds three numbers and never does anything with them. Ticking it would cost one
	// registration per decorated object per frame for no work at all.
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = false;
}

int32 UMazeObjectIdComponent::GetPlacementId(const AActor* Actor)
{
	if (!Actor)
	{
		return 0;
	}

	const UMazeObjectIdComponent* Component = Actor->FindComponentByClass<UMazeObjectIdComponent>();
	return Component ? Component->PlacementId : 0;
}
