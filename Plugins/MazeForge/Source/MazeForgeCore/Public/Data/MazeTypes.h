#pragma once

#include "CoreMinimal.h"
#include "MazeTypes.generated.h"

/**
 *  Maze cell type. Extend it freely: everything further down the pipeline
 *  asks only IsSolid()/IsPassable(), never the concrete value.
 */
UENUM(BlueprintType)
enum class EMazeCellType : uint8
{
	/** Emptiness — passable space. */
	Empty  = 0,
	/** Wall/solid mass of the maze. */
	Solid  = 1,
	/** Slab between floors. Geometrically the same as Solid, but split out for decor. */
	Floor  = 2,

	// Ladder, Spawn and Marker are legacy values. The grid describes mass and nothing else —
	// ladders, spawn points, decor, items, enemies and teleports are placements, and placements
	// belong to the spawner, which keeps its own asset and its own brushes. A cell type can say
	// "there is a ladder here" but not which blueprint, which direction it faces or where along
	// the column it starts, so the grid was the wrong place to hold any of it.
	//
	// The values stay in the enum, hidden from every dropdown, because they are serialised as
	// raw bytes. Deleting them would leave cells in already-saved grids holding a number that
	// no longer names anything. Nothing writes them any more; nothing has ever read them.
	Ladder = 3 UMETA(Hidden),
	Spawn  = 4 UMETA(Hidden),
	Marker = 5 UMETA(Hidden),
	/**
	 *  Painted back wall: the surface that closes the maze off from behind.
	 *
	 *  It produces geometry but is NOT solid: it lives in its own slice at the far edge of
	 *  the depth, the player never reaches it, and it must stay out of passability. A window
	 *  is simply a cell where the back wall was not painted — through the hole you see the sky.
	 */
	BackWall = 6
};

/**
 *  Depth band along the Y axis (§2.35 of the spec).
 *  The camera sits at +Y and looks towards −Y, so Foreground is closer to the viewer.
 */
UENUM(BlueprintType)
enum class EMazeDepthBand : uint8
{
	/** Far plane: decor, no collision. */
	Background = 0,
	/** Play band: all geometry and all collision. The player is locked in its centre. */
	Play       = 1,
	/** Near plane: decor in front of the player, no collision. */
	Foreground = 2
};

/** A single grid cell. Keep it small — there are hundreds of thousands of them. */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeCell
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	EMazeCellType Type = EMazeCellType::Empty;

	/** Index into the UMazeBuildSettings palette: which mesh/material to place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	uint8 PaletteIndex = 0;

	FMazeCell() = default;
	explicit FMazeCell(EMazeCellType InType, uint8 InPalette = 0)
		: Type(InType), PaletteIndex(InPalette) {}

	/** Whether the cell produces geometry and (in the Play band) collision. */
	bool IsSolid() const
	{
		return Type == EMazeCellType::Solid || Type == EMazeCellType::Floor;
	}

	/** Whether the player can stand in this cell. */
	bool IsPassable() const { return !IsSolid(); }

	/**
	 *  A painted back wall. Deliberately not part of IsSolid().
	 *
	 *  It gives geometry but takes no part in passability: the flood fill and the reachability
	 *  checks work in the play plane, and a wall behind the maze must not look like an obstacle
	 *  to them.
	 */
	bool IsBackWall() const { return Type == EMazeCellType::BackWall; }

	/** Whether the cell produces geometry at all — solid mass or a back wall. */
	bool HasGeometry() const { return IsSolid() || IsBackWall(); }

	bool operator==(const FMazeCell& Other) const
	{
		return Type == Other.Type && PaletteIndex == Other.PaletteIndex;
	}
};

/**
 *  World borders: whether the maze is closed off at the edges of the grid.
 *
 *  Like the backdrop, this is derived geometry — drawing a frame by hand around the
 *  whole perimeter of the map is pointless. An open side is left free so it can be
 *  joined to a neighbouring maze later.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeWorldBorders
{
	GENERATED_BODY()

	/** −X, start of the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borders")
	bool bCloseLeft = true;

	/** +X, end of the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borders")
	bool bCloseRight = true;

	/** −Z, floor of the world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borders")
	bool bCloseBottom = true;

	/** +Z, ceiling of the world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borders")
	bool bCloseTop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borders", meta = (ClampMin = "1", UIMax = "8"))
	int32 ThicknessCells = 1;
};

/**
 *  Depth profile: how N cells along Y are split into three bands.
 *
 *      +Y  FOREGROUND (ForegroundCells)  decor, no collision      <- closer to the camera
 *          PLAY       (PlayCells)        geometry + collision     <- player in the centre
 *      0   BACKGROUND (BackgroundCells)  decor, no collision
 *
 *  World zero along Y is placed at the centre of PLAY so that the stock
 *  SetPlaneConstraintNormal(0,1,0) on ASideScrollingCharacter works with the
 *  default origin (decision C2 in the spec).
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeDepthProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "0", UIMax = "16"))
	int32 BackgroundCells = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "1", UIMax = "16"))
	int32 PlayCells = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "0", UIMax = "16"))
	int32 ForegroundCells = 2;

	/** Whether to fill the far plane with generated geometry (the maze backdrop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth|Fill")
	bool bFillBackground = true;

	/** The play band is always filled, save for exotic cases. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth|Fill")
	bool bFillPlay = true;

	/**
	 *  Off by default: an empty near plane is exactly the "cutaway" of the original,
	 *  plus it is the designer's workspace for foreground decor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth|Fill")
	bool bFillForeground = false;

	/**
	 *  A solid backdrop at the far boundary, in cells.
	 *
	 *  It is built across the whole area of the room regardless of what the designer drew
	 *  and blocks the view through corridors: a corridor is a hole all the way through the
	 *  depth, and without a backdrop the camera looks through it past the maze at the skybox.
	 *
	 *  Off by default: closing the world off is usually handled well enough by the borders
	 *  (see FMazeWorldBorders), and a solid slab across the whole map is not always needed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "0", UIMax = "8"))
	int32 BackdropCells = 0;

	/** Collision margin around the Play band — the character capsule radius, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth", meta = (ClampMin = "0", Units = "cm"))
	float CollisionPaddingUU = 50.0f;

	int32 TotalCells() const { return BackgroundCells + PlayCells + ForegroundCells; }

	/** First cell of the Play band (inclusive). */
	int32 PlayStartCell() const { return BackgroundCells; }

	/** The cell right past the Play band (exclusive). */
	int32 PlayEndCell() const { return BackgroundCells + PlayCells; }

	/** Offset in cells that puts the centre of the Play band at world zero along Y. */
	double PlayCenterInCells() const { return BackgroundCells + PlayCells * 0.5; }

	/**
	 *  The slice the flat layout lives in.
	 *
	 *  The designer draws the maze in a single plane, and the volume along the depth is
	 *  grown in a separate step. We keep the plane in the middle of the play band: what was
	 *  drawn is visible exactly where the geometry will end up, and the Left view does not lie.
	 */
	int32 PlaneCellY() const
	{
		return FMath::Clamp((PlayStartCell() + PlayEndCell()) / 2,
			0, FMath::Max(0, TotalCells() - 1));
	}

	/** Whether the slice falls inside the solid backdrop. */
	bool IsBackdrop(int32 CellY) const { return CellY < FMath::Min(BackdropCells, BackgroundCells); }

	/**
	 *  The slice the painted back wall lives in: the first cell past the solid backdrop.
	 *
	 *  With no backdrop that is the far-most cell, cell 0. With a backdrop it sits right in
	 *  front of it, so the two can coexist instead of one swallowing the other.
	 *
	 *  The back wall deliberately does NOT live in the drawing plane. The maze and the wall
	 *  behind it are two independent layers over the same XZ, and the brush switches between
	 *  them by paint type — the designer keeps looking at the same view either way.
	 */
	int32 BackWallPlaneCellY() const
	{
		return FMath::Clamp(FMath::Min(BackdropCells, BackgroundCells),
			0, FMath::Max(0, TotalCells() - 1));
	}

	EMazeDepthBand GetBand(int32 CellY) const
	{
		if (CellY < PlayStartCell()) { return EMazeDepthBand::Background; }
		if (CellY < PlayEndCell())   { return EMazeDepthBand::Play; }
		return EMazeDepthBand::Foreground;
	}

	bool IsFillBand(EMazeDepthBand Band) const
	{
		switch (Band)
		{
		case EMazeDepthBand::Background: return bFillBackground;
		case EMazeDepthBand::Play:       return bFillPlay;
		case EMazeDepthBand::Foreground: return bFillForeground;
		default:                         return false;
		}
	}

	/** Whether collision must be built for this band. Play only (decision C1 in the spec). */
	static bool BandHasCollision(EMazeDepthBand Band) { return Band == EMazeDepthBand::Play; }
};
