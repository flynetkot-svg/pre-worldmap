#include "Spawner/MazeDoors.h"

#include "Assets/MazeObjectLibrary.h"
#include "Data/MazeGrid.h"

namespace MazeDoors
{
	namespace
	{
		bool DoorBlocked(const FMazeGrid& Grid, int32 SliceY, int32 X, int32 Z)
		{
			const FIntVector C(X, SliceY, Z);
			return !Grid.IsInside(C) || Grid.IsSolid(C) || Grid.IsBorderCell(C);
		}
	}

	bool IsDoor(const UMazeObjectLibrary* Library, const FMazePlacement& Placement)
	{
		const FMazeObjectType* Type = Library ? Library->FindType(Placement.TypeId) : nullptr;
		return Type && Type->bSlidingDoor;
	}

	bool PickBottom(const FMazeGrid& Grid, int32 SliceY, const FIntPoint& Cell, int32 Tolerance, int32& OutBottom)
	{
		if (DoorBlocked(Grid, SliceY, Cell.X, Cell.Y))
		{
			for (int32 Up = 1; Up <= Tolerance; ++Up)
			{
				if (!DoorBlocked(Grid, SliceY, Cell.X, Cell.Y + Up))
				{
					OutBottom = Cell.Y + Up;
					return true;
				}
			}
			return false;
		}
		for (int32 Down = 0; Down <= Tolerance; ++Down)
		{
			if (DoorBlocked(Grid, SliceY, Cell.X, Cell.Y - Down - 1))
			{
				OutBottom = Cell.Y - Down;
				return true;
			}
		}
		OutBottom = Cell.Y;
		return true;
	}

	int32 PickTop(const FMazeGrid& Grid, int32 SliceY, int32 X, int32 Bottom, int32 Z, int32 Tolerance)
	{
		// In mass: the last free row under it.
		int32 Top = Z;
		while (Top > Bottom && DoorBlocked(Grid, SliceY, X, Top))
		{
			--Top;
		}
		// Near the ceiling: right up to it.
		for (int32 Up = 0; Up <= Tolerance; ++Up)
		{
			if (DoorBlocked(Grid, SliceY, X, Top + Up + 1))
			{
				return FMath::Max(Bottom, Top + Up);
			}
			if (DoorBlocked(Grid, SliceY, X, Top + Up))
			{
				break;
			}
		}
		return FMath::Max(Bottom, Top);
	}

	bool IsClear(const FMazeGrid& Grid, int32 SliceY, int32 X, int32 Width, int32 Bottom, int32 Top, FString* OutReason)
	{
		for (int32 Dx = 0; Dx < FMath::Max(1, Width); ++Dx)
		{
			for (int32 Z = Bottom; Z <= Top; ++Z)
			{
				if (DoorBlocked(Grid, SliceY, X + Dx, Z))
				{
					if (OutReason)
					{
						*OutReason = FString::Printf(TEXT("mass at X %d Z %d inside the doorway"), X + Dx, Z);
					}
					return false;
				}
			}
		}
		return true;
	}

	int32 MeasureHeight(const FMazeGrid& Grid, int32 SliceY, int32 X, int32 Bottom)
	{
		int32 Height = 0;
		while (Height < 64 && !DoorBlocked(Grid, SliceY, X, Bottom + Height))
		{
			++Height;
		}
		return FMath::Max(1, Height);
	}

	FIntRect Rect(const FMazePlacement& Door, int32 Width)
	{
		return FIntRect(Door.CellXZ, Door.CellXZ + FIntPoint(FMath::Max(1, Width), FMath::Max(1, Door.HeightCells)));
	}
}
