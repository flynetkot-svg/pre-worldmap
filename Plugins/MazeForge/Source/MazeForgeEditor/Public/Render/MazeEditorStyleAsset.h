#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MazeEditorStyleAsset.generated.h"

/** A single overlay line: visibility, colour, thickness. */
USTRUCT(BlueprintType)
struct MAZEFORGEEDITOR_API FMazeLineStyle
{
	GENERATED_BODY()

	FMazeLineStyle() = default;

	FMazeLineStyle(const FLinearColor& InColor, float InThickness)
		: Color(InColor)
		, Thickness(InThickness)
	{
	}

	/** Clear the tick and the line is not drawn at all. Cheaper than painting it transparent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line")
	bool bVisible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line",
		meta = (ClampMin = "0.5", UIMax = "12.0"))
	float Thickness = 1.0f;
};

/** Which plane the room frames are drawn in. */
UENUM()
enum class EMazeRoomFramePlane : uint8
{
	/**
	 *  In front of all the geometry, on the camera side (+Y).
	 *
	 *  The frame does not intersect the preview cubes and does not flicker, but at a large depth
	 *  it drifts far from the drawing plane: with a 9/4/9 profile that is half a metre of world.
	 */
	FrontOfMaze = 0,

	/** In the plane of the active slice, next to the grid overlay. Always where you are drawing. */
	ActiveSlice = 1
};

/**
 *  Style preset for the mode's overlay: the colours and thicknesses of every helper line.
 *
 *  A separate asset, because how readable the grid overlay is depends on the maze material and
 *  on the scene lighting, and everyone's are different. The colours used to be constants in two
 *  .cpp files — changing them meant rebuilding.
 *
 *  The default values match the former constants: an empty Style field in the mode panel means
 *  exactly the look we had before this asset existed.
 */
UCLASS(BlueprintType)
class MAZEFORGEEDITOR_API UMazeEditorStyleAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// -------------------------------------------------------------- grid overlay

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	FMazeLineStyle GridMinor = FMazeLineStyle(FLinearColor(0.34f, 0.34f, 0.40f, 1.0f), 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	FMazeLineStyle GridMajor = FMazeLineStyle(FLinearColor(0.56f, 0.56f, 0.66f, 1.0f), 2.0f);

	/** A heavy line every N grid steps — counting cells by eye is easier that way. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid",
		meta = (ClampMin = "2", UIMax = "32"))
	int32 MajorLineEvery = 8;

	/**
	 *  The maximum number of grid overlay lines per frame. Beyond that the step doubles.
	 *
	 *  This is not aesthetics but protection: on a 707x376 map, with no thinning out, there
	 *  would be more than a thousand lines in the frame on every camera movement.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid",
		meta = (ClampMin = "50", UIMax = "2000"))
	int32 MaxGridLines = 400;

	/**
	 *  The outer bounds of the maze: the rectangle 0..SizeXZ in the slice plane.
	 *
	 *  On a 707x376 map the edge of the working area is otherwise not visible at all — the grid
	 *  overlay runs past it and breaks off where the frame ended, not where the level ended.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	FMazeLineStyle MazeBounds = FMazeLineStyle(FLinearColor(0.95f, 0.95f, 1.00f, 1.0f), 4.0f);

	// ---------------------------------------------------------------- depth bands

	/** The bounds of the play band: it is the only one with collision. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Depth Bands")
	FMazeLineStyle BandPlay = FMazeLineStyle(FLinearColor(0.20f, 0.85f, 0.40f, 1.0f), 3.0f);

	/** The outer bounds of the depth axis: the room for decoration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Depth Bands")
	FMazeLineStyle BandDecor = FMazeLineStyle(FLinearColor(0.30f, 0.55f, 0.90f, 1.0f), 1.0f);

	// --------------------------------------------------------------------- rooms

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rooms")
	FMazeLineStyle RoomBounds = FMazeLineStyle(FLinearColor(1.00f, 0.55f, 0.10f, 1.0f), 2.0f);

	/** The room under the cursor: you see at once which level what you draw will end up in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rooms")
	FMazeLineStyle RoomHovered = FMazeLineStyle(FLinearColor(1.00f, 0.90f, 0.30f, 1.0f), 5.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rooms")
	EMazeRoomFramePlane RoomFramePlane = EMazeRoomFramePlane::ActiveSlice;

	// --------------------------------------------------------------------- brush

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Brush")
	FMazeLineStyle BrushPaint = FMazeLineStyle(FLinearColor(0.20f, 0.90f, 0.35f, 1.0f), 2.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Brush")
	FMazeLineStyle BrushErase = FMazeLineStyle(FLinearColor(0.95f, 0.25f, 0.20f, 1.0f), 2.0f);

	/** The frame of the Ctrl + drag rectangle fill. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Brush")
	FMazeLineStyle BoxDrag = FMazeLineStyle(FLinearColor(0.30f, 0.70f, 1.00f, 1.0f), 3.0f);

	// ------------------------------------------------------------------- objects

	/**
	 *  The marker of a placed object: a frame with a cross through it.
	 *
	 *  The colour here is not used — each placement is drawn in its own type's colour, so the
	 *  map says what is where. Only the thickness and the visibility come from this entry.
	 *
	 *  A cross rather than a plain frame, because a plain frame is what the room bounds already
	 *  are, and a lone rectangle over a grid of rectangles reads as part of the grid.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objects")
	FMazeLineStyle ObjectMarker = FMazeLineStyle(FLinearColor::White, 3.0f);
};
