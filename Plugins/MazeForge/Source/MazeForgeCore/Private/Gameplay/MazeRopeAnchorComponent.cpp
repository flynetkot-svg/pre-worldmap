#include "Gameplay/MazeRopeAnchorComponent.h"

#include "Engine/World.h"

void UMazeRopeAnchorComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UMazeRopeAnchorSubsystem* Registry = UMazeRopeAnchorSubsystem::Get(this))
	{
		Registry->Register(this);
	}
}

void UMazeRopeAnchorComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UMazeRopeAnchorSubsystem* Registry = UMazeRopeAnchorSubsystem::Get(this))
	{
		Registry->Unregister(this);
	}
	Super::EndPlay(Reason);
}

UMazeRopeAnchorComponent* UMazeRopeAnchorSubsystem::FindNear(const FVector& Point, float Radius) const
{
	UMazeRopeAnchorComponent* Best = nullptr;
	double BestDistance = FMath::Square(static_cast<double>(Radius));
	for (const TWeakObjectPtr<UMazeRopeAnchorComponent>& Weak : Anchors)
	{
		UMazeRopeAnchorComponent* Anchor = Weak.Get();
		if (!Anchor)
		{
			continue;
		}
		const double Distance = FVector::DistSquared(Anchor->GetComponentLocation(), Point);
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = Anchor;
		}
	}
	return Best;
}

UMazeRopeAnchorSubsystem* UMazeRopeAnchorSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UMazeRopeAnchorSubsystem>() : nullptr;
}
