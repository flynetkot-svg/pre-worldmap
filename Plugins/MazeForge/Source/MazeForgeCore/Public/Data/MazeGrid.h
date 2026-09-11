#pragma once

#include "CoreMinimal.h"
#include "Data/MazeTypes.h"
#include "MazeGrid.generated.h"

/**
 *  Voxel grid of the maze — the single source of truth.
 *
 *  Axes (confirmed against the ASideScrollingCharacter code):
 *      X — along the level, Z — height, Y — depth (the camera axis).
 *
 *  Storage is sparse: the maze is mostly empty, and edits are made cell by cell.
 *  An empty cell = no entry in Cells.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeGrid
{
	GENERATED_BODY()

	/** Size of a single cell in centimetres. Non-uniform scale is allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid", meta = (AllowPreserveRatio))
	FVector CellSize = FVector(100.0, 100.0, 100.0);

	/** Bounds of the working area in cells: X = length, Y = height (world Z). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid", meta = (ClampMin = "1"))
	FIntPoint SizeXZ = FIntPoint(256, 128);

	/** Layout of the depth into Background / Play / Foreground bands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FMazeDepthProfile Depth;

	/** Whether the world is closed off at the edges of the grid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FMazeWorldBorders Borders;

	/** Shift of the whole grid in the world. Along Y the Play centre is already accounted for; this is an extra offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FVector WorldOrigin = FVector::ZeroVector;

	/**
	 *  Occupied cells. Deliberately without an Edit specifier: the Details panel must not
	 *  try to draw hundreds of thousands of rows.
	 */
	UPROPERTY()
	TMap<FIntVector, FMazeCell> Cells;

	// ------------------------------------------------------------- dimensions

	int32 DepthCells() const { return Depth.TotalCells(); }
	FIntVector Dimensions() const { return FIntVector(SizeXZ.X, DepthCells(), SizeXZ.Y); }
	int32 NumCells() const { return Cells.Num(); }

	bool IsInside(const FIntVector& C) const
	{
		return C.X >= 0 && C.X < SizeXZ.X
			&& C.Y >= 0 && C.Y < DepthCells()
			&& C.Z >= 0 && C.Z < SizeXZ.Y;
	}

	// ---------------------------------------------------------------- access

	const FMazeCell* Find(const FIntVector& C) const { return Cells.Find(C); }

	FMazeCell Get(const FIntVector& C) const
	{
		const FMazeCell* Found = Cells.Find(C);
		return Found ? *Found : FMazeCell();
	}

	bool IsSolid(const FIntVector& C) const
	{
		const FMazeCell* Found = Cells.Find(C);
		return Found && Found->IsSolid();
	}

	bool IsPassable(const FIntVector& C) const { return !IsSolid(C); }

	/**
	 *  Solidity from the point of view of the mesh bake: the drawn cells plus the backdrop.
	 *  Player passability is computed through IsSolid and ignores the backdrop — it lies
	 *  far behind the play band and does not affect routes.
	 */
	/** Whether the cell lies inside a closed world border. */
	bool IsBorderCell(const FIntVector& C) const
	{
		const int32 T = FMath::Max(1, Borders.ThicknessCells);

		if (Borders.bCloseLeft   && C.X < T)                { return true; }
		if (Borders.bCloseRight  && C.X >= SizeXZ.X - T)    { return true; }
		if (Borders.bCloseBottom && C.Z < T)                { return true; }
		if (Borders.bCloseTop    && C.Z >= SizeXZ.Y - T)    { return true; }

		return false;
	}

	/** A painted back wall in this cell. */
	bool IsBackWall(const FIntVector& C) const
	{
		const FMazeCell* Cell = Cells.Find(C);
		return Cell && Cell->IsBackWall();
	}

	/**
	 *  Everything that turns into geometry: solid mass, derived slabs and the painted back wall.
	 *
	 *  Deliberately wider than IsSolid(). The back wall must be built and must cull the faces
	 *  of its neighbours, but it must never look like an obstacle to the reachability checks —
	 *  hence two separate predicates rather than one.
	 */
	bool IsSolidForGeometry(const FIntVector& C) const
	{
		return Depth.IsBackdrop(C.Y) || IsBorderCell(C) || IsSolid(C) || IsBackWall(C);
	}

	// ------------------------------------------------- does a thing of this size fit here
	//
	//  Three geometric questions, deliberately with no idea what is being placed. The spawner
	//  composes them into "can this crate stand here"; the answer to "does an agent of height N
	//  fit" is IsBoxPassable with a footprint of 1 x N. Keeping the interpretation out of the
	//  grid is what lets one rule serve the brush, the export and the generator alike.
	//
	//  FMazeLayout2D has its own CanStand and keeps it: that buffer exists during generation,
	//  before there is a grid at all. The two must agree on the rule, and this is the copy that
	//  everything after generation uses.

	/** Every cell of the XZ rectangle is passable in this slice. Max exclusive. */
	bool IsBoxPassable(const FIntPoint& MinXZ, const FIntPoint& ExtentXZ, int32 SliceY) const;

	/** A horizontal run of cells is solid. For the floor under a footprint, or the ceiling over. */
	bool IsRowSolid(int32 MinX, int32 MaxX, int32 Z, int32 SliceY) const;

	/** A vertical run of cells is solid. For the wall a footprint leans against. */
	bool IsColumnSolid(int32 X, int32 MinZ, int32 MaxZ, int32 SliceY) const;

	/** true if the entry actually changed. */
	bool Set(const FIntVector& C, const FMazeCell& Cell);
	bool Clear(const FIntVector& C);

	/** Rectangular fill. Min inclusive, Max exclusive. Returns the number of changed cells. */
	int32 SetBox(const FIntVector& Min, const FIntVector& Max, const FMazeCell& Cell);
	int32 ClearBox(const FIntVector& Min, const FIntVector& Max);

	/** Fill across the whole depth, honouring the profile's bFill* flags. */
	int32 SetColumnXZ(int32 X, int32 Z, const FMazeCell& Cell);
	int32 ClearColumnXZ(int32 X, int32 Z);

	void Reset() { Cells.Reset(); }

	/**
	 *  A cell with all six neighbours Solid is not visible from any side and is thrown out
	 *  before the mesh bake. This removes the bulk of the geometry inside walls.
	 *
	 *  Going past the grid boundary counts as emptiness, not as solid: otherwise the edge
	 *  layer would be treated as enclosed and would disappear. There is exactly one
	 *  exception — the far face along −Y, which the side camera never sees.
	 */
	bool IsFullyEnclosed(const FIntVector& C) const;

	// -------------------------------------------------------- world coordinates

	/** Centre of a cell in world coordinates. */
	FVector CellToWorld(const FIntVector& C) const;

	/** The cell containing a point. It may land outside the bounds — check IsInside. */
	FIntVector WorldToCell(const FVector& World) const;

	FBox GetCellBounds(const FIntVector& C) const;

	/** Bounds of the whole working area (not just the occupied cells). */
	FBox GetWorldBounds() const;

	/** Bounds of a rectangular XZ patch across the whole depth. Max exclusive. */
	FBox GetBoxWorldBounds(const FIntPoint& MinXZ, const FIntPoint& MaxXZ) const;

	/** World Y of the plane the player runs in. By construction always WorldOrigin.Y. */
	double GetPlayPlaneY() const { return WorldOrigin.Y; }

	/** Range of cells along Y for which collision is built (the Play band + the capsule radius). */
	void GetCollisionCellRangeY(int32& OutMinY, int32& OutMaxY) const;

	EMazeDepthBand GetBand(const FIntVector& C) const { return Depth.GetBand(C.Y); }
};
