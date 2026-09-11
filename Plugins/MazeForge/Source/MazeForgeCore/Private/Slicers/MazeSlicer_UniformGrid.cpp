#include "Slicers/MazeSlicer_UniformGrid.h"

#include "Data/MazeGrid.h"
#include "Data/MazeRoomDesc.h"

#define LOCTEXT_NAMESPACE "MazeForge"

FText UMazeSlicer_UniformGrid::GetDisplayName() const
{
	return LOCTEXT("SlicerUniform", "Uniform Grid");
}

void UMazeSlicer_UniformGrid::Slice(const FMazeGrid& Grid, TArray<FMazeRoomDesc>& OutRooms) const
{
	const FIntPoint Size = Grid.SizeXZ;
	const FIntPoint Step(FMath::Max(1, RoomSizeXZ.X), FMath::Max(1, RoomSizeXZ.Y));

	// We start at the lattice node nearest to zero so that the Origin shift does not
	// cut off the edge of the map.
	int32 StartX = OriginXZ.X % Step.X;
	int32 StartZ = OriginXZ.Y % Step.Y;
	if (StartX > 0) { StartX -= Step.X; }
	if (StartZ > 0) { StartZ -= Step.Y; }

	int32 IndexX = 0;
	for (int32 X = StartX; X < Size.X; X += Step.X, ++IndexX)
	{
		int32 IndexZ = 0;
		for (int32 Z = StartZ; Z < Size.Y; Z += Step.Y, ++IndexZ)
		{
			FMazeRoomDesc Room;
			Room.MinXZ = FIntPoint(FMath::Max(0, X), FMath::Max(0, Z));
			Room.MaxXZ = FIntPoint(FMath::Min(Size.X, X + Step.X), FMath::Min(Size.Y, Z + Step.Y));

			if (Room.MaxXZ.X <= Room.MinXZ.X || Room.MaxXZ.Y <= Room.MinXZ.Y)
			{
				continue;
			}

			Room.RoomId = FName(*FString::Printf(TEXT("R_%03d_%03d"), IndexX, IndexZ));
			OutRooms.Add(MoveTemp(Room));
		}
	}
}

#undef LOCTEXT_NAMESPACE
