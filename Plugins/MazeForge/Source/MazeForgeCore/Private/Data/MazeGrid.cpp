#include "Data/MazeGrid.h"

bool FMazeGrid::Set(const FIntVector& C, const FMazeCell& Cell)
{
	if (!IsInside(C))
	{
		return false;
	}

	if (Cell.Type == EMazeCellType::Empty)
	{
		return Clear(C);
	}

	if (const FMazeCell* Existing = Cells.Find(C))
	{
		if (*Existing == Cell)
		{
			return false;
		}
	}

	Cells.Add(C, Cell);
	return true;
}

bool FMazeGrid::Clear(const FIntVector& C)
{
	return Cells.Remove(C) > 0;
}

int32 FMazeGrid::SetBox(const FIntVector& Min, const FIntVector& Max, const FMazeCell& Cell)
{
	int32 Changed = 0;
	for (int32 X = Min.X; X < Max.X; ++X)
	{
		for (int32 Y = Min.Y; Y < Max.Y; ++Y)
		{
			for (int32 Z = Min.Z; Z < Max.Z; ++Z)
			{
				Changed += Set(FIntVector(X, Y, Z), Cell) ? 1 : 0;
			}
		}
	}
	return Changed;
}

int32 FMazeGrid::ClearBox(const FIntVector& Min, const FIntVector& Max)
{
	int32 Changed = 0;
	for (int32 X = Min.X; X < Max.X; ++X)
	{
		for (int32 Y = Min.Y; Y < Max.Y; ++Y)
		{
			for (int32 Z = Min.Z; Z < Max.Z; ++Z)
			{
				Changed += Clear(FIntVector(X, Y, Z)) ? 1 : 0;
			}
		}
	}
	return Changed;
}

int32 FMazeGrid::SetColumnXZ(int32 X, int32 Z, const FMazeCell& Cell)
{
	int32 Changed = 0;
	const int32 Total = DepthCells();
	for (int32 Y = 0; Y < Total; ++Y)
	{
		if (Depth.IsFillBand(Depth.GetBand(Y)))
		{
			Changed += Set(FIntVector(X, Y, Z), Cell) ? 1 : 0;
		}
	}
	return Changed;
}

int32 FMazeGrid::ClearColumnXZ(int32 X, int32 Z)
{
	int32 Changed = 0;
	const int32 Total = DepthCells();
	for (int32 Y = 0; Y < Total; ++Y)
	{
		Changed += Clear(FIntVector(X, Y, Z)) ? 1 : 0;
	}
	return Changed;
}

bool FMazeGrid::IsBoxPassable(const FIntPoint& MinXZ, const FIntPoint& ExtentXZ, int32 SliceY) const
{
	// ExtentXZ and not SizeXZ: the grid already has a member by that name, and a parameter that
	// shadows it reads as the map's dimensions right up until it does the wrong thing.
	const int32 Width = FMath::Max(1, ExtentXZ.X);
	const int32 Height = FMath::Max(1, ExtentXZ.Y);

	for (int32 X = MinXZ.X; X < MinXZ.X + Width; ++X)
	{
		for (int32 Z = MinXZ.Y; Z < MinXZ.Y + Height; ++Z)
		{
			const FIntVector Cell(X, SliceY, Z);

			// Outside the grid is not passable. An object half off the map is not a placement
			// that "mostly works" — it is one whose other half has nowhere to be.
			//
			// The world border is not passable either, and it has to be said here explicitly
			// because no cell was ever painted there — IsSolid knows nothing about it. The
			// support checks below already count the border as mass, and for a while these two
			// disagreed: IsRowSolid called the border something to stand on, IsBoxPassable
			// called it empty space to stand in. Both answers were yes for a cell inside the
			// border, so the generator laid a tidy row of crates along the bottom of the map,
			// buried inside a wall that the export builds for real. It looked like the objects
			// had sunk into the geometry, which is exactly what had happened.
			if (!IsInside(Cell) || IsSolid(Cell) || IsBorderCell(Cell))
			{
				return false;
			}
		}
	}

	return true;
}

bool FMazeGrid::IsRowSolid(int32 MinX, int32 MaxX, int32 Z, int32 SliceY) const
{
	for (int32 X = MinX; X < MaxX; ++X)
	{
		const FIntVector Cell(X, SliceY, Z);

		// The world border counts as support: it is real mass in the built level, even though
		// no cell was ever painted there.
		if (!IsSolid(Cell) && !IsBorderCell(Cell))
		{
			return false;
		}
	}

	return MaxX > MinX;
}

bool FMazeGrid::IsColumnSolid(int32 X, int32 MinZ, int32 MaxZ, int32 SliceY) const
{
	for (int32 Z = MinZ; Z < MaxZ; ++Z)
	{
		const FIntVector Cell(X, SliceY, Z);

		if (!IsSolid(Cell) && !IsBorderCell(Cell))
		{
			return false;
		}
	}

	return MaxZ > MinZ;
}

bool FMazeGrid::IsFullyEnclosed(const FIntVector& C) const
{
	static const FIntVector Offsets[6] = {
		FIntVector( 1,  0,  0), FIntVector(-1,  0,  0),
		FIntVector( 0,  1,  0), FIntVector( 0, -1,  0),
		FIntVector( 0,  0,  1), FIntVector( 0,  0, -1)
	};

	for (const FIntVector& Offset : Offsets)
	{
		const FIntVector Neighbour = C + Offset;

		// Past the world boundary there is emptiness: the face exists, so the cell must stay.
		// There used to be an exception here for the far face along −Y — "nobody sees it
		// anyway" — but it left the mesh open, and that breaks the normals along the edge,
		// distance fields and everything else that counts on a closed volume.
		if (!IsInside(Neighbour))
		{
			return false;
		}

		if (!IsSolidForGeometry(Neighbour))
		{
			return false;
		}
	}

	return true;
}

FVector FMazeGrid::CellToWorld(const FIntVector& C) const
{
	// Along Y we subtract the Play band centre so the play plane lands on WorldOrigin.Y.
	return FVector(
		WorldOrigin.X + (C.X + 0.5) * CellSize.X,
		WorldOrigin.Y + (C.Y + 0.5 - Depth.PlayCenterInCells()) * CellSize.Y,
		WorldOrigin.Z + (C.Z + 0.5) * CellSize.Z);
}

FIntVector FMazeGrid::WorldToCell(const FVector& World) const
{
	const FVector Local = World - WorldOrigin;
	return FIntVector(
		FMath::FloorToInt32(Local.X / CellSize.X),
		FMath::FloorToInt32(Local.Y / CellSize.Y + Depth.PlayCenterInCells()),
		FMath::FloorToInt32(Local.Z / CellSize.Z));
}

FBox FMazeGrid::GetCellBounds(const FIntVector& C) const
{
	const FVector Centre = CellToWorld(C);
	return FBox(Centre - CellSize * 0.5, Centre + CellSize * 0.5);
}

FBox FMazeGrid::GetWorldBounds() const
{
	return GetBoxWorldBounds(FIntPoint::ZeroValue, SizeXZ);
}

FBox FMazeGrid::GetBoxWorldBounds(const FIntPoint& MinXZ, const FIntPoint& MaxXZ) const
{
	const double DepthOffset = Depth.PlayCenterInCells();
	const FVector Min(
		WorldOrigin.X + MinXZ.X * CellSize.X,
		WorldOrigin.Y + (0.0 - DepthOffset) * CellSize.Y,
		WorldOrigin.Z + MinXZ.Y * CellSize.Z);
	const FVector Max(
		WorldOrigin.X + MaxXZ.X * CellSize.X,
		WorldOrigin.Y + (DepthCells() - DepthOffset) * CellSize.Y,
		WorldOrigin.Z + MaxXZ.Y * CellSize.Z);
	return FBox(Min, Max);
}

void FMazeGrid::GetCollisionCellRangeY(int32& OutMinY, int32& OutMaxY) const
{
	const int32 PaddingCells = (CellSize.Y > KINDA_SMALL_NUMBER)
		? FMath::CeilToInt32(Depth.CollisionPaddingUU / CellSize.Y)
		: 0;

	OutMinY = FMath::Max(0, Depth.PlayStartCell() - PaddingCells);
	OutMaxY = FMath::Min(DepthCells(), Depth.PlayEndCell() + PaddingCells);
}
