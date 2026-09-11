#include "Rules/MazeRule_CameraFrame.h"

#include "Assets/MazeWorldManifest.h"

#define LOCTEXT_NAMESPACE "MazeForge"

FText UMazeRule_CameraFrame::GetDisplayName() const
{
	return LOCTEXT("RuleCameraFrame", "Camera Frame");
}

void UMazeRule_CameraFrame::Score(const FMazeStreamQuery& Query,
                                  const UMazeWorldManifest& Manifest,
                                  TMap<FName, float>& InOutDesire) const
{
	// The view rectangle in the XZ plane, expanded by the margin.
	const double HalfX = Query.CameraHalfExtentXZ.X + MarginUU;
	const double HalfZ = Query.CameraHalfExtentXZ.Y + MarginUU;

	const FBox Frame(
		FVector(Query.CameraLocation.X - HalfX, -HALF_WORLD_MAX, Query.CameraLocation.Z - HalfZ),
		FVector(Query.CameraLocation.X + HalfX,  HALF_WORLD_MAX, Query.CameraLocation.Z + HalfZ));

	for (const FMazeRoomEntry& Room : Manifest.Rooms)
	{
		if (!Room.WorldBounds.IsValid)
		{
			continue;
		}

		// We test the intersection in XZ: in depth the rooms and the frame always overlap.
		const bool bInFrame =
			Room.WorldBounds.Max.X >= Frame.Min.X && Room.WorldBounds.Min.X <= Frame.Max.X &&
			Room.WorldBounds.Max.Z >= Frame.Min.Z && Room.WorldBounds.Min.Z <= Frame.Max.Z;

		if (bInFrame)
		{
			Accumulate(InOutDesire, Room.RoomId, 1.0f);
			continue;
		}

		const float Distance = DistanceToRoomXZ(Query.CameraLocation, Room.WorldBounds);
		Accumulate(InOutDesire, Room.RoomId, 1.0f - Distance / FMath::Max(1.0f, FalloffUU));
	}
}

#undef LOCTEXT_NAMESPACE
