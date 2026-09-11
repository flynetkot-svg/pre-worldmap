#include "Actors/MazeRoomAnchor.h"

#include "Components/SceneComponent.h"

AMazeRoomAnchor::AMazeRoomAnchor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetActorEnableCollision(false);
}
