#pragma once

#include "CoreMinimal.h"

class FPrimitiveDrawInterface;
class UMazeEditorStyleAsset;
class UMazeObjectLibrary;
struct FMazeGrid;
struct FMazeLineStyle;
struct FMazePlacement;
struct FMazeRoomDesc;

/**
 *  Drawing the mode's helper graphics through the PDI: the grid overlay, the cursor,
 *  the depth band bounds, the room bounds and the box fill frame.
 *
 *  The range of cells is computed by the caller (from the visible area of the viewport), not by
 *  the renderer: the grid overlay must cover the whole screen without turning into tens of
 *  thousands of lines when zoomed far out — that is what Step is for.
 *
 *  The colours and thicknesses come from the style preset and are not hard-coded here: how
 *  readable the overlay is depends on the maze material and the scene lighting.
 */
class FMazeGridRenderer
{
public:
	/** Picks a grid step (1, 2, 4, 8...) that keeps the line count within the limit. */
	static int32 ChooseStep(const FIntPoint& MinXZ, const FIntPoint& MaxXZ, int32 MaxGridLines);

	static void DrawGrid(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid, int32 SliceY,
	                     const FIntPoint& MinXZ, const FIntPoint& MaxXZ, int32 Step,
	                     const UMazeEditorStyleAsset& Style);

	/**
	 *  The outer bounds of the maze's working area.
	 *
	 *  It is drawn across the full width of the grid, not across the visible part: this is the
	 *  one overlay line whose job is to show where the level ends, not where the frame ended.
	 *  It is never thinned out — four lines at any zoom.
	 */
	static void DrawMazeBounds(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid, int32 SliceY,
	                           const UMazeEditorStyleAsset& Style);

	static void DrawCellHighlight(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid,
	                              const FIntVector& Min, const FIntVector& Max,
	                              const FMazeLineStyle& Line);

	/**
	 *  The placed objects, as the footprint rectangle of each in its type's colour.
	 *
	 *  Rectangles through the PDI rather than instanced cubes like the cell preview: there are
	 *  tens of these, not hundreds of thousands, and a frame shows the footprint — the thing
	 *  that decides whether a placement is legal — where a solid block would hide it.
	 */
	static void DrawPlacements(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid,
	                           const UMazeObjectLibrary* Library,
	                           const TArray<FMazePlacement>& Placements,
	                           const UMazeEditorStyleAsset& Style);

	static void DrawDepthBands(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid,
	                           const FIntPoint& MinXZ, const FIntPoint& MaxXZ,
	                           const UMazeEditorStyleAsset& Style);

	/**
	 *  The bounds of the streaming rooms.
	 *
	 *  They are drawn as a flat frame rather than a volumetric bound: a room takes the whole
	 *  depth, so the edges of a volumetric box would run through the preview cubes and cause
	 *  z-fighting — the frame flickers and gets lost. Which plane exactly is decided by the
	 *  preset: in front of the geometry, or in the slice where the drawing happens.
	 */
	//  The room list is passed in rather than read off the asset, because it is not always the
	//  asset's. Preview Rooms draws a slicing that was computed and never stored.
	static void DrawRooms(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid,
	                      const TArray<FMazeRoomDesc>& Rooms,
	                      const FIntPoint& MinXZ, const FIntPoint& MaxXZ,
	                      FName HighlightRoomId, int32 SliceY,
	                      const UMazeEditorStyleAsset& Style);
};
