#include "Rules/MazeRule_VerticalMotion.h"

#include "Assets/MazeWorldManifest.h"

#define LOCTEXT_NAMESPACE "MazeForge"

FText UMazeRule_VerticalMotion::GetDisplayName() const
{
	return LOCTEXT("RuleVertical", "Vertical Motion");
}

void UMazeRule_VerticalMotion::Score(const FMazeStreamQuery& Query,
                                     const UMazeWorldManifest& Manifest,
                                     TMap<FName, float>& InOutDesire) const
{
	TArray<FVector, TInlineAllocator<2>> Probes;

	switch (Query.MotionMode)
	{
	case EMazeMotionMode::Falling:
	{
		// The free-fall height at the current vertical speed: v^2 / 2g.
		const float FallSpeed = FMath::Abs(static_cast<float>(Query.PlayerVelocity.Z));
		const float Gravity = FMath::Max(1.0f, FMath::Abs(Query.GravityZ));
		const float DropUU = FMath::Min(FallSpeed * FallSpeed / (2.0f * Gravity), MaxFallProbeUU);

		Probes.Add(Query.PlayerLocation - FVector(0.0, 0.0, DropUU));
		break;
	}

	case EMazeMotionMode::Climbing:
		Probes.Add(Query.PlayerLocation + FVector(0.0, 0.0, ClimbProbeUU));
		Probes.Add(Query.PlayerLocation - FVector(0.0, 0.0, ClimbProbeUU));
		break;

	default:
		// While walking the vertical adds nothing: the velocity prediction handles that.
		return;
	}

	for (const FMazeRoomEntry& Room : Manifest.Rooms)
	{
		if (!Room.WorldBounds.IsValid)
		{
			continue;
		}

		float Best = 0.0f;
		for (const FVector& Probe : Probes)
		{
			const float Distance = DistanceToRoomXZ(Probe, Room.WorldBounds);
			Best = FMath::Max(Best, 1.0f - Distance / FMath::Max(1.0f, FalloffUU));
		}

		Accumulate(InOutDesire, Room.RoomId, Best);
	}
}

#undef LOCTEXT_NAMESPACE
