#include "Rules/MazeRule_VelocityPredict.h"

#include "Assets/MazeWorldManifest.h"

#define LOCTEXT_NAMESPACE "MazeForge"

FText UMazeRule_VelocityPredict::GetDisplayName() const
{
	return LOCTEXT("RuleVelocity", "Velocity Predict");
}

void UMazeRule_VelocityPredict::Score(const FMazeStreamQuery& Query,
                                      const UMazeWorldManifest& Manifest,
                                      TMap<FName, float>& InOutDesire) const
{
	const float Speed = static_cast<float>(Query.PlayerVelocity.Size2D());
	if (Speed < 1.0f)
	{
		return;
	}

	const float SpeedAlpha = FMath::Clamp(Speed / FMath::Max(1.0f, ReferenceSpeed), 0.0f, 1.0f);
	const float LookAhead = FMath::Lerp(LookAheadSeconds, MaxLookAheadSeconds, SpeedAlpha);

	const FVector Probe = Query.PlayerLocation + Query.PlayerVelocity * LookAhead;

	for (const FMazeRoomEntry& Room : Manifest.Rooms)
	{
		if (!Room.WorldBounds.IsValid)
		{
			continue;
		}

		const float Distance = DistanceToRoomXZ(Probe, Room.WorldBounds);
		Accumulate(InOutDesire, Room.RoomId, 1.0f - Distance / FMath::Max(1.0f, FalloffUU));
	}
}

#undef LOCTEXT_NAMESPACE
