#include "Slicers/MazeRoomSlicerBase.h"

#include "Data/MazeGrid.h"
#include "Data/MazeRoomDesc.h"
#include "MazeForgeCore.h"

#define LOCTEXT_NAMESPACE "MazeForge"

void UMazeRoomSlicerBase::Execute(const FMazeGrid& Grid, TArray<FMazeRoomDesc>& OutRooms) const
{
	OutRooms.Reset();

	Slice(Grid, OutRooms);
	Finalize(Grid, OutRooms, bDiscardEmptyRooms);
	BuildPortalGraph(Grid, OutRooms);

	UE_LOG(LogMazeForge, Log, TEXT("%s: %d rooms produced."),
		*GetDisplayName().ToString(), OutRooms.Num());
}

FText UMazeRoomSlicerBase::GetDisplayName() const
{
	return LOCTEXT("SlicerBase", "Slicer");
}

void UMazeRoomSlicerBase::Finalize(const FMazeGrid& Grid, TArray<FMazeRoomDesc>& InOutRooms, bool bDiscardEmpty)
{
	const int32 DepthCells = Grid.DepthCells();

	for (FMazeRoomDesc& Room : InOutRooms)
	{
		Room.SolidCellCount = 0;

		for (int32 X = Room.MinXZ.X; X < Room.MaxXZ.X; ++X)
		{
			for (int32 Z = Room.MinXZ.Y; Z < Room.MaxXZ.Y; ++Z)
			{
				for (int32 Y = 0; Y < DepthCells; ++Y)
				{
					// Back wall counts as content, not as emptiness. A room can legitimately hold
					// nothing but the wall behind the maze — the roof of a building, where the far
					// surface carries aerials and distant silhouettes. Counting only solid mass would
					// discard that room, no level would be created, and the wall would silently vanish.
					const FIntVector Cell(X, Y, Z);
					if (Grid.IsSolid(Cell) || Grid.IsBackWall(Cell))
					{
						++Room.SolidCellCount;
					}
				}
			}
		}

		Room.WorldBounds = Grid.GetBoxWorldBounds(Room.MinXZ, Room.MaxXZ);
	}

	if (bDiscardEmpty)
	{
		InOutRooms.RemoveAll([](const FMazeRoomDesc& Room)
		{
			return Room.SolidCellCount == 0;
		});
	}
}

int32 UMazeRoomSlicerBase::FindRoomIndexAtXZ(const TArray<FMazeRoomDesc>& Rooms, int32 X, int32 Z)
{
	for (int32 Index = 0; Index < Rooms.Num(); ++Index)
	{
		if (Rooms[Index].ContainsXZ(X, Z))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UMazeRoomSlicerBase::BuildPortalGraph(const FMazeGrid& Grid, TArray<FMazeRoomDesc>& InOutRooms)
{
	for (FMazeRoomDesc& Room : InOutRooms)
	{
		Room.Neighbors.Reset();
	}

	// We check passability only in the play band: the decor at the edges of the depth
	// is not a path for the player.
	const int32 PlayMinY = Grid.Depth.PlayStartCell();
	const int32 PlayMaxY = Grid.Depth.PlayEndCell();

	auto TryLink = [&InOutRooms, &Grid, PlayMinY, PlayMaxY]
		(int32 RoomIndex, int32 X, int32 Z, int32 NeighbourX, int32 NeighbourZ)
	{
		bool bPassable = false;
		for (int32 Y = PlayMinY; Y < PlayMaxY && !bPassable; ++Y)
		{
			bPassable = Grid.IsPassable(FIntVector(X, Y, Z))
				&& Grid.IsPassable(FIntVector(NeighbourX, Y, NeighbourZ));
		}

		if (!bPassable)
		{
			return;
		}

		const int32 OtherIndex = FindRoomIndexAtXZ(InOutRooms, NeighbourX, NeighbourZ);
		if (OtherIndex == INDEX_NONE || OtherIndex == RoomIndex)
		{
			return;
		}

		InOutRooms[RoomIndex].Neighbors.AddUnique(InOutRooms[OtherIndex].RoomId);
		InOutRooms[OtherIndex].Neighbors.AddUnique(InOutRooms[RoomIndex].RoomId);
	};

	for (int32 Index = 0; Index < InOutRooms.Num(); ++Index)
	{
		const FMazeRoomDesc Room = InOutRooms[Index];

		for (int32 Z = Room.MinXZ.Y; Z < Room.MaxXZ.Y; ++Z)
		{
			TryLink(Index, Room.MinXZ.X,     Z, Room.MinXZ.X - 1, Z);
			TryLink(Index, Room.MaxXZ.X - 1, Z, Room.MaxXZ.X,     Z);
		}

		for (int32 X = Room.MinXZ.X; X < Room.MaxXZ.X; ++X)
		{
			TryLink(Index, X, Room.MinXZ.Y,     X, Room.MinXZ.Y - 1);
			TryLink(Index, X, Room.MaxXZ.Y - 1, X, Room.MaxXZ.Y);
		}
	}
}

#undef LOCTEXT_NAMESPACE
