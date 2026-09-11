#include "Assets/MazeWorldManifest.h"

const FMazeTransitionPoint* UMazeWorldManifest::FindTransition(const int32 Id) const
{
	if (Id == 0)
	{
		return nullptr;
	}

	return Transitions.FindByPredicate([Id](const FMazeTransitionPoint& Point)
	{
		return Point.Id == Id;
	});
}

const FMazeRoomEntry* UMazeWorldManifest::FindRoom(FName RoomId) const
{
	return Rooms.FindByPredicate([RoomId](const FMazeRoomEntry& Entry)
	{
		return Entry.RoomId == RoomId;
	});
}

FName UMazeWorldManifest::FindRoomAt(const FVector& WorldLocation) const
{
	FName Nearest;
	double NearestDistanceSq = TNumericLimits<double>::Max();

	for (const FMazeRoomEntry& Entry : Rooms)
	{
		if (!Entry.WorldBounds.IsValid)
		{
			continue;
		}

		const double DX = FMath::Max3(Entry.WorldBounds.Min.X - WorldLocation.X, 0.0,
			WorldLocation.X - Entry.WorldBounds.Max.X);
		const double DZ = FMath::Max3(Entry.WorldBounds.Min.Z - WorldLocation.Z, 0.0,
			WorldLocation.Z - Entry.WorldBounds.Max.Z);

		if (DX == 0.0 && DZ == 0.0)
		{
			return Entry.RoomId;
		}

		// The point is outside every room — we take the nearest one, not nothing.
		//
		// An empty CurrentRoomId switches off two things at once: pinning the room under
		// the player, and the passage graph rule which is the only thing that guarantees
		// the neighbours get loaded. And ending up outside is easy for the player: standing
		// on the roof of the maze, in a gap between rooms, or simply because the capsule is
		// centred above the surface. Silently losing the whole graph over that is worse than
		// picking the wrong room at a boundary.
		const double DistanceSq = DX * DX + DZ * DZ;
		if (DistanceSq < NearestDistanceSq)
		{
			NearestDistanceSq = DistanceSq;
			Nearest = Entry.RoomId;
		}
	}

	return Nearest;
}
