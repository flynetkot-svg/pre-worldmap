#include "Rules/MazeStreamingRule.h"

#define LOCTEXT_NAMESPACE "MazeForge"

FText UMazeStreamingRule::GetDisplayName() const
{
	return LOCTEXT("StreamingRuleBase", "Streaming Rule");
}

void UMazeStreamingRule::Accumulate(TMap<FName, float>& InOutDesire, FName RoomId, float RawScore) const
{
	if (RoomId.IsNone() || RawScore <= 0.0f)
	{
		return;
	}

	const float Contribution = FMath::Clamp(RawScore, 0.0f, 1.0f) * Weight;
	float& Existing = InOutDesire.FindOrAdd(RoomId, 0.0f);
	Existing = FMath::Max(Existing, Contribution);
}

float UMazeStreamingRule::DistanceToRoomXZ(const FVector& Point, const FBox& RoomBounds)
{
	const double DX = FMath::Max3(RoomBounds.Min.X - Point.X, 0.0, Point.X - RoomBounds.Max.X);
	const double DZ = FMath::Max3(RoomBounds.Min.Z - Point.Z, 0.0, Point.Z - RoomBounds.Max.Z);

	return static_cast<float>(FMath::Sqrt(DX * DX + DZ * DZ));
}

#undef LOCTEXT_NAMESPACE