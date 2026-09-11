#include "Rules/MazeRule_Radius.h"

#include "Assets/MazeWorldManifest.h"

#define LOCTEXT_NAMESPACE "MazeForge"

FText UMazeRule_Radius::GetDisplayName() const
{
	return LOCTEXT("RuleRadius", "Player Radius");
}

void UMazeRule_Radius::Score(const FMazeStreamQuery& Query,
                             const UMazeWorldManifest& Manifest,
                             TMap<FName, float>& InOutDesire) const
{
	for (const FMazeRoomEntry& Room : Manifest.Rooms)
	{
		if (!Room.WorldBounds.IsValid)
		{
			continue;
		}

		const float Distance = DistanceToRoomXZ(Query.PlayerLocation, Room.WorldBounds);
		Accumulate(InOutDesire, Room.RoomId, 1.0f - Distance / FMath::Max(1.0f, RadiusUU));
	}
}

#undef LOCTEXT_NAMESPACE
