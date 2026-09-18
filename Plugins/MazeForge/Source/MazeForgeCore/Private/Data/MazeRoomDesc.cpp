#include "Data/MazeRoomDesc.h"

namespace MazeRooms
{
	int32 IndexAtXZ(const TArray<FMazeRoomDesc>& Rooms, const int32 X, const int32 Z)
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

	const FMazeRoomDesc* FindAtXZ(const TArray<FMazeRoomDesc>& Rooms, const FIntPoint& CellXZ)
	{
		const int32 Index = IndexAtXZ(Rooms, CellXZ.X, CellXZ.Y);
		return Rooms.IsValidIndex(Index) ? &Rooms[Index] : nullptr;
	}
}
