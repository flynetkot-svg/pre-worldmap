#include "Gameplay/MazeProgressSubsystem.h"

#include "Engine/World.h"

void UMazeProgressSubsystem::MarkUsed(int32 PlacementId)
{
	// Zero is an actor placed by hand in a level: it has no identity to remember it by.
	if (PlacementId != 0)
	{
		UsedPlacements.Add(PlacementId);
	}
}

bool UMazeProgressSubsystem::IsUsed(int32 PlacementId) const
{
	return PlacementId != 0 && UsedPlacements.Contains(PlacementId);
}

void UMazeProgressSubsystem::SetState(int32 PlacementId, int32 State)
{
	if (PlacementId != 0)
	{
		PlacementStates.Add(PlacementId, State);
	}
}

bool UMazeProgressSubsystem::GetState(int32 PlacementId, int32& OutState) const
{
	if (const int32* Found = PlacementId != 0 ? PlacementStates.Find(PlacementId) : nullptr)
	{
		OutState = *Found;
		return true;
	}
	return false;
}

UMazeProgressSubsystem* UMazeProgressSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UMazeProgressSubsystem>() : nullptr;
}
