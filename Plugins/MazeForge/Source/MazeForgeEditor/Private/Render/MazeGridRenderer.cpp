#include "Render/MazeGridRenderer.h"

#include "Assets/MazeGridAsset.h"
#include "Assets/MazeObjectLibrary.h"
#include "Data/MazeGrid.h"
#include "Spawner/MazePlacementRules.h"
#include "Render/MazeEditorStyleAsset.h"
#include "SceneManagement.h"

namespace
{
	/** The two diagonals of an XZ rectangle. What turns a frame into a marker. */
	void DrawCrossXZ(FPrimitiveDrawInterface* PDI, double X0, double X1, double Z0, double Z1,
	                 double PlaneY, const FMazeLineStyle& Line)
	{
		PDI->DrawLine(FVector(X0, PlaneY, Z0), FVector(X1, PlaneY, Z1),
			Line.Color, SDPG_Foreground, Line.Thickness);
		PDI->DrawLine(FVector(X0, PlaneY, Z1), FVector(X1, PlaneY, Z0),
			Line.Color, SDPG_Foreground, Line.Thickness);
	}

	/**
	 *  Parallel diagonals clipped to an XZ rectangle, at a fixed world Y.
	 *
	 *  Spacing is fixed in world units, not a fixed line count. A count that fits the
	 *  rectangle keeps the picture tidy at every size and is exactly wrong for this job: a
	 *  merge spanning half the map got the same ten strokes as a two-cell one, which at map
	 *  scale is a few faint scratches over a room full of geometry. Constant spacing means
	 *  the texture reads the same everywhere, and a big area simply gets more of it.
	 *
	 *  One family of lines and not two. Crossed hatching at this density reads as a solid
	 *  wash and buries the maze underneath, which defeats the point of marking part of it.
	 *  Mirror swaps which diagonal is used — the cheapest way to tell two overlaid meanings
	 *  apart without spending a colour on it.
	 */
	void DrawHatchXZ(FPrimitiveDrawInterface* PDI, double X0, double X1, double Z0, double Z1,
	                 double PlaneY, const FMazeLineStyle& Line, double SpacingUU, bool bMirror,
	                 int32 MaxLines)
	{
		if (X1 <= X0 || Z1 <= Z0 || SpacingUU <= 0.0)
		{
			return;
		}

		// x - z = C for one diagonal, x + z = C for the other. Both sweep C across the range
		// the rectangle occupies along that axis.
		const double First = bMirror ? (X0 + Z0) : (X0 - Z1);
		const double Last = bMirror ? (X1 + Z1) : (X1 - Z0);

		// The cap is protection, not taste: zoomed out over a merge covering a 700-cell map,
		// honest spacing would ask for thousands of lines every frame.
		const int32 Count = FMath::Min(
			FMath::FloorToInt((Last - First) / SpacingUU), FMath::Max(1, MaxLines));

		for (int32 Index = 1; Index <= Count; ++Index)
		{
			const double C = First + SpacingUU * static_cast<double>(Index);

			// Clipped against both pairs of edges: a diagonal enters the rectangle only where
			// its x is inside as well as its z.
			const double ZEnter = bMirror ? FMath::Max(Z0, C - X1) : FMath::Max(Z0, X0 - C);
			const double ZExit = bMirror ? FMath::Min(Z1, C - X0) : FMath::Min(Z1, X1 - C);
			if (ZExit <= ZEnter)
			{
				continue;
			}

			const double XEnter = bMirror ? (C - ZEnter) : (ZEnter + C);
			const double XExit = bMirror ? (C - ZExit) : (ZExit + C);

			PDI->DrawLine(FVector(XEnter, PlaneY, ZEnter), FVector(XExit, PlaneY, ZExit),
				Line.Color, SDPG_Foreground, Line.Thickness);
		}
	}

	/** A rectangle in the XZ plane at a given world Y. Four lines, no fill. */
	void DrawRectXZ(FPrimitiveDrawInterface* PDI, double X0, double X1, double Z0, double Z1,
	                double PlaneY, const FMazeLineStyle& Line)
	{
		const FVector A(X0, PlaneY, Z0);
		const FVector B(X1, PlaneY, Z0);
		const FVector C(X1, PlaneY, Z1);
		const FVector D(X0, PlaneY, Z1);

		// SDPG_Foreground rather than World: the overlay planes cut straight through the
		// geometry, and in the normal depth group a line passes the depth test one moment and
		// fails it the next — the overlay flickers at the slightest camera movement.
		PDI->DrawLine(A, B, Line.Color, SDPG_Foreground, Line.Thickness);
		PDI->DrawLine(B, C, Line.Color, SDPG_Foreground, Line.Thickness);
		PDI->DrawLine(C, D, Line.Color, SDPG_Foreground, Line.Thickness);
		PDI->DrawLine(D, A, Line.Color, SDPG_Foreground, Line.Thickness);
	}
}

int32 FMazeGridRenderer::ChooseStep(const FIntPoint& MinXZ, const FIntPoint& MaxXZ,
                                    int32 MaxGridLines)
{
	const int32 CountX = FMath::Max(1, MaxXZ.X - MinXZ.X);
	const int32 CountZ = FMath::Max(1, MaxXZ.Y - MinXZ.Y);
	const int32 Limit = FMath::Max(16, MaxGridLines);

	int32 Step = 1;
	while ((CountX / Step + CountZ / Step) > Limit && Step < 1024)
	{
		Step *= 2;
	}
	return Step;
}

void FMazeGridRenderer::DrawGrid(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid, int32 SliceY,
                                 const FIntPoint& MinXZ, const FIntPoint& MaxXZ, int32 Step,
                                 const UMazeEditorStyleAsset& Style)
{
	if (MaxXZ.X <= MinXZ.X || MaxXZ.Y <= MinXZ.Y || Step <= 0)
	{
		return;
	}

	if (!Style.GridMinor.bVisible && !Style.GridMajor.bVisible)
	{
		return;
	}

	// The drawing plane is the active depth slice.
	const double PlaneY = Grid.CellToWorld(FIntVector(0, SliceY, 0)).Y;

	auto CornerAt = [&Grid, PlaneY](int32 X, int32 Z)
	{
		return FVector(
			Grid.WorldOrigin.X + X * Grid.CellSize.X,
			PlaneY,
			Grid.WorldOrigin.Z + Z * Grid.CellSize.Z);
	};

	const int32 MajorStep = Step * FMath::Max(2, Style.MajorLineEvery);

	// We start from a node that is a multiple of the step, otherwise the major lines "swim"
	// while scrolling.
	const int32 FirstX = FMath::DivideAndRoundDown(MinXZ.X, Step) * Step;
	const int32 FirstZ = FMath::DivideAndRoundDown(MinXZ.Y, Step) * Step;

	for (int32 X = FirstX; X <= MaxXZ.X; X += Step)
	{
		const FMazeLineStyle& Line = ((X % MajorStep) == 0) ? Style.GridMajor : Style.GridMinor;
		if (!Line.bVisible)
		{
			continue;
		}

		PDI->DrawLine(CornerAt(X, MinXZ.Y), CornerAt(X, MaxXZ.Y),
			Line.Color, SDPG_Foreground, Line.Thickness);
	}

	for (int32 Z = FirstZ; Z <= MaxXZ.Y; Z += Step)
	{
		const FMazeLineStyle& Line = ((Z % MajorStep) == 0) ? Style.GridMajor : Style.GridMinor;
		if (!Line.bVisible)
		{
			continue;
		}

		PDI->DrawLine(CornerAt(MinXZ.X, Z), CornerAt(MaxXZ.X, Z),
			Line.Color, SDPG_Foreground, Line.Thickness);
	}
}

void FMazeGridRenderer::DrawMazeBounds(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid,
                                       int32 SliceY, const UMazeEditorStyleAsset& Style)
{
	if (!Style.MazeBounds.bVisible || Grid.SizeXZ.X <= 0 || Grid.SizeXZ.Y <= 0)
	{
		return;
	}

	const double PlaneY = Grid.CellToWorld(FIntVector(0, SliceY, 0)).Y;

	DrawRectXZ(PDI,
		Grid.WorldOrigin.X,
		Grid.WorldOrigin.X + Grid.SizeXZ.X * Grid.CellSize.X,
		Grid.WorldOrigin.Z,
		Grid.WorldOrigin.Z + Grid.SizeXZ.Y * Grid.CellSize.Z,
		PlaneY, Style.MazeBounds);
}

void FMazeGridRenderer::DrawCellHighlight(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid,
                                          const FIntVector& Min, const FIntVector& Max,
                                          const FMazeLineStyle& Line)
{
	if (!Line.bVisible)
	{
		return;
	}

	const FBox Bounds = Grid.GetCellBounds(Min) + Grid.GetCellBounds(Max);
	DrawWireBox(PDI, Bounds, Line.Color, SDPG_Foreground, Line.Thickness);
}

void FMazeGridRenderer::DrawCellHatch(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid,
                                      const FIntVector& Min, const FIntVector& Max,
                                      const FMazeLineStyle& Line, float SpacingCells, bool bMirror,
                                      bool bCrossed)
{
	if (!Line.bVisible)
	{
		return;
	}

	const FBox Bounds = Grid.GetCellBounds(Min) + Grid.GetCellBounds(Max);

	// Spacing is given in cells so the density is the designer's, in the units he draws in,
	// and stays the same when the cell size changes.
	const double SpacingUU = FMath::Max(0.1, SpacingCells) * Grid.CellSize.X;

	DrawHatchXZ(PDI, Bounds.Min.X, Bounds.Max.X, Bounds.Min.Z, Bounds.Max.Z,
		Bounds.Max.Y, Line, SpacingUU, bMirror, 512);

	// The second family turns stripes into a wash. Drawn after the first rather than
	// interleaved, so that turning it off leaves the first one exactly as it was.
	if (bCrossed)
	{
		DrawHatchXZ(PDI, Bounds.Min.X, Bounds.Max.X, Bounds.Min.Z, Bounds.Max.Z,
			Bounds.Max.Y, Line, SpacingUU, !bMirror, 512);
	}
}

void FMazeGridRenderer::DrawDepthBands(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid,
                                       const FIntPoint& MinXZ, const FIntPoint& MaxXZ,
                                       const UMazeEditorStyleAsset& Style)
{
	if (MaxXZ.X <= MinXZ.X || MaxXZ.Y <= MinXZ.Y)
	{
		return;
	}

	const double X0 = Grid.WorldOrigin.X + MinXZ.X * Grid.CellSize.X;
	const double X1 = Grid.WorldOrigin.X + MaxXZ.X * Grid.CellSize.X;
	const double Z0 = Grid.WorldOrigin.Z + MinXZ.Y * Grid.CellSize.Z;
	const double Z1 = Grid.WorldOrigin.Z + MaxXZ.Y * Grid.CellSize.Z;

	// It matters to the designer to see where the play band starts: it is the only one with
	// collision, everything else along the depth axis is room for decoration.
	auto DrawPlane = [&](int32 CellY, const FMazeLineStyle& Line)
	{
		if (!Line.bVisible)
		{
			return;
		}

		const double DepthOffset = Grid.Depth.PlayCenterInCells();
		const double PlaneY = Grid.WorldOrigin.Y + (CellY - DepthOffset) * Grid.CellSize.Y;

		DrawRectXZ(PDI, X0, X1, Z0, Z1, PlaneY, Line);
	};

	DrawPlane(0, Style.BandDecor);
	DrawPlane(Grid.Depth.PlayStartCell(), Style.BandPlay);
	DrawPlane(Grid.Depth.PlayEndCell(), Style.BandPlay);
	DrawPlane(Grid.DepthCells(), Style.BandDecor);
}

void FMazeGridRenderer::DrawPlacements(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid,
                                      const UMazeObjectLibrary* Library,
                                      const TArray<FMazePlacement>& Placements,
                                      const UMazeEditorStyleAsset& Style)
{
	if (!Style.ObjectMarker.bVisible)
	{
		return;
	}

	for (const FMazePlacement& Placement : Placements)
	{
		const FMazeObjectType* Type = Library ? Library->FindType(Placement.TypeId) : nullptr;

		// A placement whose type has gone from the library still gets drawn, in red. Silently
		// skipping it would hide the one thing worth knowing: that it will spawn nothing.
		const FIntPoint Size = Type
			? FIntPoint(FMath::Max(1, Type->FootprintCells.X), FMath::Max(1, Type->FootprintCells.Y))
			: FIntPoint(1, 1);

		FMazeLineStyle Line(Type ? Type->EditorColor : FLinearColor(1.0f, 0.1f, 0.1f),
			Style.ObjectMarker.Thickness);

		const int32 SliceY = MazePlacement::BandSliceY(Grid, Placement.Band);

		const FBox Box = Grid.GetCellBounds(FIntVector(Placement.CellXZ.X, SliceY, Placement.CellXZ.Y))
			+ Grid.GetCellBounds(FIntVector(Placement.CellXZ.X + Size.X - 1, SliceY,
				Placement.CellXZ.Y + Size.Y - 1));

		DrawRectXZ(PDI, Box.Min.X, Box.Max.X, Box.Min.Z, Box.Max.Z, Box.GetCenter().Y, Line);

		// The cross is inset by a fraction of a cell so its ends do not sit exactly on the frame:
		// two lines meeting at a corner at this thickness read as a blob, not as a corner.
		const double InsetX = (Box.Max.X - Box.Min.X) * 0.18;
		const double InsetZ = (Box.Max.Z - Box.Min.Z) * 0.18;

		DrawCrossXZ(PDI, Box.Min.X + InsetX, Box.Max.X - InsetX,
			Box.Min.Z + InsetZ, Box.Max.Z - InsetZ, Box.GetCenter().Y, Line);
	}
}

void FMazeGridRenderer::DrawRooms(FPrimitiveDrawInterface* PDI, const FMazeGrid& Grid,
                                  const TArray<FMazeRoomDesc>& Rooms,
                                  const FIntPoint& MinXZ, const FIntPoint& MaxXZ,
                                  FName HighlightRoomId, int32 SliceY,
                                  const UMazeEditorStyleAsset& Style,
                                  const FMazeLineStyle* BoundsOverride)
{
	const FMazeLineStyle& Bounds = BoundsOverride ? *BoundsOverride : Style.RoomBounds;

	if (!Bounds.bVisible && !Style.RoomHovered.bVisible)
	{
		return;
	}

	// Where to draw the frame. FrontOfMaze puts it in front of the geometry on the camera side:
	// the frame does not intersect the cubes and does not flicker, but with a depth of 22 cells
	// it drifts half a metre away from the drawing plane and in perspective drifts apart from
	// the grid overlay. ActiveSlice puts it exactly where the brush is working.
	const double PlaneY = (Style.RoomFramePlane == EMazeRoomFramePlane::ActiveSlice)
		? Grid.CellToWorld(FIntVector(0, SliceY, 0)).Y
		: Grid.WorldOrigin.Y
			+ (Grid.DepthCells() - Grid.Depth.PlayCenterInCells()) * Grid.CellSize.Y
			+ Grid.CellSize.Y * 0.5;

	for (const FMazeRoomDesc& Room : Rooms)
	{
		if (!Room.WorldBounds.IsValid)
		{
			continue;
		}

		// There is no point drawing off-screen: with a hundred rooms that is hundreds of
		// wasted lines.
		if (Room.MaxXZ.X < MinXZ.X || Room.MinXZ.X > MaxXZ.X
			|| Room.MaxXZ.Y < MinXZ.Y || Room.MinXZ.Y > MaxXZ.Y)
		{
			continue;
		}

		const bool bHighlight = !HighlightRoomId.IsNone() && Room.RoomId == HighlightRoomId;
		const FMazeLineStyle& Line = bHighlight ? Style.RoomHovered : Bounds;
		if (!Line.bVisible)
		{
			continue;
		}

		DrawRectXZ(PDI,
			Room.WorldBounds.Min.X, Room.WorldBounds.Max.X,
			Room.WorldBounds.Min.Z, Room.WorldBounds.Max.Z,
			PlaneY, Line);
	}
}
