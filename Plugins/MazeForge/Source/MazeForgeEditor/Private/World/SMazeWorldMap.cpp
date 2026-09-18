#include "World/SMazeWorldMap.h"

#include "AssetRegistry/AssetData.h"
#include "Assets/MazeWorldGraph.h"
#include "Assets/MazeWorldManifest.h"
#include "MazeForgeCore.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MazeForgeEditor"

const FName SMazeWorldMap::TabId(TEXT("MazeForgeWorldMap"));

TWeakPtr<SMazeWorldMap> SMazeWorldMap::LiveInstance;
TWeakObjectPtr<UMazeWorldGraph> SMazeWorldMap::PendingGraph;

namespace
{
	/** Height of one maze schematic, in canvas units. Width follows from its aspect. */
	constexpr float NodeHeight = 190.0f;
	constexpr float NodeMinWidth = 90.0f;
	constexpr float NodeMaxWidth = 420.0f;
	constexpr float NodeGap = 28.0f;
	constexpr float Margin = 16.0f;
	constexpr float LabelHeight = 18.0f;

	/** Half the side of a transition square, and how close a click has to be to count. */
	constexpr float PointHalf = 6.0f;
	constexpr float PointGrab = 9.0f;
	constexpr float LinkGrab = 6.0f;

	/**
	 *  How wide a row of mazes is allowed to grow before the next one wraps.
	 *
	 *  A fixed number now, where it used to be the width of the window. The window is no longer
	 *  the thing being filled — the canvas is, and the view moves over it — so a layout that
	 *  reshuffled itself every time the panel was dragged wider would be reshuffling for no
	 *  reason, and would do it on every zoom notch as well.
	 */
	constexpr float CanvasRowWidth = 1400.0f;

	constexpr float MinZoom = 0.15f;
	constexpr float MaxZoom = 4.0f;

	/** One wheel notch. Multiplicative, so zooming out undoes zooming in exactly. */
	constexpr float ZoomStep = 1.15f;

	/** Canvas units between grid lines, and how many of them make a heavier one. */
	constexpr float GridStep = 50.0f;
	constexpr int32 GridMajorEvery = 4;

	/** Empty canvas kept around the content, so nodes at the edge are not flush against it. */
	constexpr float CanvasPadding = 60.0f;

	const FLinearColor GridMinorColor(1.0f, 1.0f, 1.0f, 0.045f);
	const FLinearColor GridMajorColor(1.0f, 1.0f, 1.0f, 0.10f);

	const FLinearColor FrameColor(0.55f, 0.45f, 0.20f);
	const FLinearColor RoomColor(0.42f, 0.34f, 0.15f);
	const FLinearColor EntryColor(0.90f, 0.16f, 0.16f);
	const FLinearColor GateColor(0.16f, 0.42f, 0.95f);
	const FLinearColor LinkColor(0.75f, 0.75f, 0.80f);
	const FLinearColor BrokenColor(1.00f, 0.30f, 0.30f);
	const FLinearColor SelectedColor(1.00f, 0.85f, 0.25f);
	const FLinearColor LabelColor(0.75f, 0.75f, 0.78f);

	void AddRect(FSlateWindowElementList& Out, int32 Layer, const FPaintGeometry& Geometry,
	             const FVector2f& Min, const FVector2f& Max, const FLinearColor& Color,
	             float Thickness)
	{
		TArray<FVector2f> Points;
		Points.Reserve(5);
		Points.Add(FVector2f(Min.X, Min.Y));
		Points.Add(FVector2f(Max.X, Min.Y));
		Points.Add(FVector2f(Max.X, Max.Y));
		Points.Add(FVector2f(Min.X, Max.Y));
		Points.Add(FVector2f(Min.X, Min.Y));

		FSlateDrawElement::MakeLines(Out, Layer, Geometry, MoveTemp(Points),
			ESlateDrawEffect::None, Color, true, Thickness);
	}

	void AddSegment(FSlateWindowElementList& Out, int32 Layer, const FPaintGeometry& Geometry,
	                const FVector2f& A, const FVector2f& B, const FLinearColor& Color,
	                float Thickness)
	{
		TArray<FVector2f> Points;
		Points.Reserve(2);
		Points.Add(A);
		Points.Add(B);

		FSlateDrawElement::MakeLines(Out, Layer, Geometry, MoveTemp(Points),
			ESlateDrawEffect::None, Color, true, Thickness);
	}

	/** Distance from a point to a segment. Used to decide whether a click landed on a link. */
	float DistanceToSegment(const FVector2f& P, const FVector2f& A, const FVector2f& B)
	{
		const FVector2f AB = B - A;
		const float LengthSq = AB.SizeSquared();

		if (LengthSq <= KINDA_SMALL_NUMBER)
		{
			return (P - A).Size();
		}

		const float T = FMath::Clamp(FVector2f::DotProduct(P - A, AB) / LengthSq, 0.0f, 1.0f);
		return (P - (A + AB * T)).Size();
	}
}

// ===================================================================== the canvas

void SMazeWorldMapCanvas::Construct(const FArguments& InArgs)
{
	Graph = InArgs._Graph;
	OnSelectionChanged = InArgs._OnSelectionChanged;
}

FVector2f SMazeWorldMapCanvas::ToCanvas(const FMazeMapNode& Node, const FVector& World)
{
	const FVector2f Size = Node.WorldMax - Node.WorldMin;

	const float U = Size.X > KINDA_SMALL_NUMBER
		? (static_cast<float>(World.X) - Node.WorldMin.X) / Size.X : 0.5f;
	const float V = Size.Y > KINDA_SMALL_NUMBER
		? (static_cast<float>(World.Z) - Node.WorldMin.Y) / Size.Y : 0.5f;

	// Z is up in the world and down on the screen, so V is subtracted from the bottom. Getting
	// this the other way round draws a maze that is upside down but perfectly plausible, which
	// is the worst kind of wrong.
	return FVector2f(
		Node.Rect.Left + U * (Node.Rect.Right - Node.Rect.Left),
		Node.Rect.Bottom - V * (Node.Rect.Bottom - Node.Rect.Top));
}

void SMazeWorldMapCanvas::RebuildLayout() const
{
	Nodes.Reset();
	Points.Reset();

	UMazeWorldGraph* Live = Graph.Get();
	if (!Live)
	{
		return;
	}

	const float Available = CanvasRowWidth;

	float PenX = Margin;
	float PenY = Margin;

	for (const TSoftObjectPtr<UMazeWorldManifest>& Entry : Live->Mazes)
	{
		// Loaded, not merely resolved. This used to ask only whether the manifest happened to
		// be in memory already, on the reasoning that a map should not pull a dozen assets off
		// disk to paint a frame — which would be right if it happened every frame. It does not:
		// the first paint loads them and every one after that finds them loaded.
		//
		// What the old way actually produced was a map that was empty on a fresh editor start,
		// and links declared dead because the maze at the other end had not been opened yet.
		// A manifest is a list of rooms and transition points with no cells in it; ten of them
		// cost less than the confusion of a world map that shows nothing.
		UMazeWorldManifest* Manifest = Entry.LoadSynchronous();
		if (!Manifest || !Manifest->WorldBounds.IsValid)
		{
			continue;
		}

		FMazeMapNode Node;
		Node.Manifest = Manifest;
		Node.WorldMin = FVector2f(static_cast<float>(Manifest->WorldBounds.Min.X),
		                          static_cast<float>(Manifest->WorldBounds.Min.Z));
		Node.WorldMax = FVector2f(static_cast<float>(Manifest->WorldBounds.Max.X),
		                          static_cast<float>(Manifest->WorldBounds.Max.Z));

		const FVector2f Size = Node.WorldMax - Node.WorldMin;
		const float Aspect = Size.Y > KINDA_SMALL_NUMBER ? Size.X / Size.Y : 1.0f;
		const float Width = FMath::Clamp(NodeHeight * Aspect, NodeMinWidth, NodeMaxWidth);

		if (const FVector2D* Placed = Live->FindLayout(Manifest))
		{
			// Dragged by hand, so the automatic flow does not get a say — and the pen is left
			// exactly where it was, so placing one maze by hand does not shuffle the rest.
			const FVector2f At(static_cast<float>(Placed->X), static_cast<float>(Placed->Y));
			Node.Rect = FSlateRect(At.X, At.Y, At.X + Width, At.Y + NodeHeight);
		}
		else
		{
			// Wrapped at a fixed width rather than at the window's: a world of thirty mazes in
			// one endless row is a world nobody can see the shape of, and the view scrolls now,
			// so the row does not have to fit on screen.
			if (PenX > Margin && PenX + Width > Margin + Available)
			{
				PenX = Margin;
				PenY += NodeHeight + LabelHeight + NodeGap;
			}

			Node.Rect = FSlateRect(PenX, PenY, PenX + Width, PenY + NodeHeight);
			PenX += Width + NodeGap;
		}

		const int32 NodeIndex = Nodes.Add(Node);

		for (const FMazeTransitionPoint& Transition : Manifest->Transitions)
		{
			FMazeMapPoint Point;
			Point.NodeIndex = NodeIndex;
			Point.Id = Transition.Id;
			Point.Role = Transition.Role;
			Point.Canvas = ToCanvas(Nodes[NodeIndex], Transition.Location);
			Points.Add(Point);
		}
	}
}

// ===================================================================== the view

FVector2f SMazeWorldMapCanvas::CanvasToScreen(const FVector2f& CanvasPos) const
{
	return (CanvasPos - ViewOffset) * Zoom;
}

FVector2f SMazeWorldMapCanvas::ScreenToCanvas(const FVector2f& ScreenPos) const
{
	return ScreenPos / FMath::Max(Zoom, KINDA_SMALL_NUMBER) + ViewOffset;
}

FSlateRect SMazeWorldMapCanvas::CanvasExtent() const
{
	if (Nodes.Num() == 0)
	{
		// Something rather than nothing, so the scroll bars have a range to divide by and the
		// clamp has somewhere to clamp to. An empty graph still draws its grid.
		return FSlateRect(0.0f, 0.0f, CanvasRowWidth, NodeHeight + LabelHeight);
	}

	FSlateRect Extent = Nodes[0].Rect;

	for (const FMazeMapNode& Node : Nodes)
	{
		Extent.Left = FMath::Min(Extent.Left, Node.Rect.Left);

		// The label sits above the rectangle and is part of what has to stay reachable.
		Extent.Top = FMath::Min(Extent.Top, Node.Rect.Top - LabelHeight);

		Extent.Right = FMath::Max(Extent.Right, Node.Rect.Right);
		Extent.Bottom = FMath::Max(Extent.Bottom, Node.Rect.Bottom);
	}

	return Extent.ExtendBy(FMargin(CanvasPadding));
}

void SMazeWorldMapCanvas::ClampView()
{
	Zoom = FMath::Clamp(Zoom, MinZoom, MaxZoom);

	const FSlateRect Extent = CanvasExtent();
	const FVector2f Visible = LastViewportSize / FMath::Max(Zoom, KINDA_SMALL_NUMBER);

	// When everything fits, the view is centred rather than pinned to a corner: a map with room
	// to spare that hugs the top-left looks like it has been scrolled somewhere by accident.
	const float ExtentWidth = Extent.Right - Extent.Left;
	const float ExtentHeight = Extent.Bottom - Extent.Top;

	ViewOffset.X = Visible.X >= ExtentWidth
		? Extent.Left - (Visible.X - ExtentWidth) * 0.5f
		: FMath::Clamp(ViewOffset.X, Extent.Left, Extent.Right - Visible.X);

	ViewOffset.Y = Visible.Y >= ExtentHeight
		? Extent.Top - (Visible.Y - ExtentHeight) * 0.5f
		: FMath::Clamp(ViewOffset.Y, Extent.Top, Extent.Bottom - Visible.Y);
}

void SMazeWorldMapCanvas::ZoomToFit()
{
	RebuildLayout();

	const FSlateRect Extent = CanvasExtent();
	const float ExtentWidth = FMath::Max(Extent.Right - Extent.Left, KINDA_SMALL_NUMBER);
	const float ExtentHeight = FMath::Max(Extent.Bottom - Extent.Top, KINDA_SMALL_NUMBER);

	Zoom = FMath::Clamp(
		FMath::Min(LastViewportSize.X / ExtentWidth, LastViewportSize.Y / ExtentHeight),
		MinZoom, MaxZoom);

	ViewOffset = FVector2f(Extent.Left, Extent.Top);
	ClampView();
}

void SMazeWorldMapCanvas::GetScrollState(const EOrientation Orientation,
                                         float& OutOffsetFraction, float& OutThumbFraction) const
{
	const FSlateRect Extent = CanvasExtent();
	const bool bHorizontal = Orientation == Orient_Horizontal;

	const float Total = bHorizontal ? Extent.Right - Extent.Left : Extent.Bottom - Extent.Top;
	const float Near = bHorizontal ? Extent.Left : Extent.Top;
	const float Offset = bHorizontal ? ViewOffset.X : ViewOffset.Y;

	const float Visible = (bHorizontal ? LastViewportSize.X : LastViewportSize.Y)
		/ FMath::Max(Zoom, KINDA_SMALL_NUMBER);

	if (Total <= KINDA_SMALL_NUMBER || Visible >= Total)
	{
		OutOffsetFraction = 0.0f;
		OutThumbFraction = 1.0f;
		return;
	}

	OutThumbFraction = FMath::Clamp(Visible / Total, 0.0f, 1.0f);
	OutOffsetFraction = FMath::Clamp((Offset - Near) / (Total - Visible), 0.0f, 1.0f)
		* (1.0f - OutThumbFraction);
}

void SMazeWorldMapCanvas::SetScrollOffsetFraction(const EOrientation Orientation,
                                                  const float Fraction)
{
	const FSlateRect Extent = CanvasExtent();
	const bool bHorizontal = Orientation == Orient_Horizontal;

	const float Total = bHorizontal ? Extent.Right - Extent.Left : Extent.Bottom - Extent.Top;
	const float Near = bHorizontal ? Extent.Left : Extent.Top;

	const float Visible = (bHorizontal ? LastViewportSize.X : LastViewportSize.Y)
		/ FMath::Max(Zoom, KINDA_SMALL_NUMBER);

	const float Travel = FMath::Max(Total - Visible, 0.0f);
	const float Target = Near + FMath::Clamp(Fraction, 0.0f, 1.0f) * Travel;

	if (bHorizontal)
	{
		ViewOffset.X = Target;
	}
	else
	{
		ViewOffset.Y = Target;
	}

	ClampView();
}

void SMazeWorldMapCanvas::PaintGrid(FSlateWindowElementList& OutDrawElements, const int32 LayerId,
                                    const FGeometry& AllottedGeometry,
                                    const FPaintGeometry& Geometry) const
{
	// Drawn in canvas space like everything else, so the spacing between lines is a fixed
	// number of canvas units at every zoom. That is what makes it a scale and not decoration:
	// the lines get further apart as you zoom in, and the squares mean the same thing.
	const FVector2f TopLeft = ScreenToCanvas(FVector2f::ZeroVector);
	const FVector2f BottomRight = ScreenToCanvas(LastViewportSize);

	// A line every GridStep would be a solid wall of them when zoomed far out. Coarsen until
	// they are at least a few pixels apart on screen, and keep the heavier lines meaningful.
	float Step = GridStep;
	while (Step * Zoom < 6.0f && Step < GridStep * 4096.0f)
	{
		Step *= static_cast<float>(GridMajorEvery);
	}

	const int32 FirstX = FMath::FloorToInt(TopLeft.X / Step);
	const int32 LastX = FMath::CeilToInt(BottomRight.X / Step);
	const int32 FirstY = FMath::FloorToInt(TopLeft.Y / Step);
	const int32 LastY = FMath::CeilToInt(BottomRight.Y / Step);

	// A sanity ceiling. A degenerate zoom should make the grid disappear, not lock the editor
	// up drawing a hundred thousand lines nobody can tell apart.
	if ((LastX - FirstX) > 512 || (LastY - FirstY) > 512)
	{
		return;
	}

	for (int32 Index = FirstX; Index <= LastX; ++Index)
	{
		const float X = Index * Step;
		const bool bMajor = (Index % GridMajorEvery) == 0;

		TArray<FVector2f> Line;
		Line.Add(FVector2f(X, TopLeft.Y));
		Line.Add(FVector2f(X, BottomRight.Y));

		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry, MoveTemp(Line),
			ESlateDrawEffect::None, bMajor ? GridMajorColor : GridMinorColor, false,
			(bMajor ? 1.5f : 1.0f) / Zoom);
	}

	for (int32 Index = FirstY; Index <= LastY; ++Index)
	{
		const float Y = Index * Step;
		const bool bMajor = (Index % GridMajorEvery) == 0;

		TArray<FVector2f> Line;
		Line.Add(FVector2f(TopLeft.X, Y));
		Line.Add(FVector2f(BottomRight.X, Y));

		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry, MoveTemp(Line),
			ESlateDrawEffect::None, bMajor ? GridMajorColor : GridMinorColor, false,
			(bMajor ? 1.5f : 1.0f) / Zoom);
	}
}

int32 SMazeWorldMapCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                                   const FSlateRect& CullingRect,
                                   FSlateWindowElementList& OutDrawElements, int32 LayerId,
                                   const FWidgetStyle& WidgetStyle, bool bParentEnabled) const
{
	RebuildLayout();

	LastViewportSize = FVector2f(AllottedGeometry.GetLocalSize());

	if (!bViewInitialised && Nodes.Num() > 0)
	{
		// Framed on the first paint that has something to frame. Opening at 1:1 on a corner of
		// an empty canvas is the map telling you nothing on the one occasion it matters most.
		const_cast<SMazeWorldMapCanvas*>(this)->ZoomToFit();
		bViewInitialised = true;
	}

	const_cast<SMazeWorldMapCanvas*>(this)->ClampView();

	// The whole of the zoom and the scroll, in one transform. Everything below draws in canvas
	// coordinates and knows nothing about either — which is why adding them did not touch a
	// single line of the drawing code.
	const FPaintGeometry Geometry = AllottedGeometry.ToPaintGeometry(
		FSlateLayoutTransform(Zoom, FVector2f(-ViewOffset * Zoom)));

	// A plain fixed size: every piece of text on this map is positioned in screen space, so it
	// is never scaled by the view and never needs compensating for.
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	PaintGrid(OutDrawElements, LayerId, AllottedGeometry, Geometry);

	UMazeWorldGraph* Live = Graph.Get();
	BrokenLinkCount = 0;

	// --- the mazes themselves

	for (const FMazeMapNode& Node : Nodes)
	{
		AddRect(OutDrawElements, LayerId, Geometry,
			FVector2f(Node.Rect.Left, Node.Rect.Top),
			FVector2f(Node.Rect.Right, Node.Rect.Bottom), FrameColor, 1.5f);

		const UMazeWorldManifest* Manifest = Node.Manifest.Get();
		if (!Manifest)
		{
			continue;
		}

		// The room grid, which is the whole of the schematic. Not the cells: the picture has to
		// say where a point is, and rooms say that at a glance where thirty thousand cells do not.
		for (const FMazeRoomEntry& Room : Manifest->Rooms)
		{
			if (!Room.WorldBounds.IsValid)
			{
				continue;
			}

			const FVector2f Min = ToCanvas(Node, FVector(Room.WorldBounds.Min.X, 0.0,
				Room.WorldBounds.Min.Z));
			const FVector2f Max = ToCanvas(Node, FVector(Room.WorldBounds.Max.X, 0.0,
				Room.WorldBounds.Max.Z));

			AddRect(OutDrawElements, LayerId, Geometry,
				FVector2f(FMath::Min(Min.X, Max.X), FMath::Min(Min.Y, Max.Y)),
				FVector2f(FMath::Max(Min.X, Max.X), FMath::Max(Min.Y, Max.Y)), RoomColor, 1.0f);
		}

		// Positioned by hand into screen space rather than drawn through the zoomed geometry,
		// and the label is the better for it: a name that shrinks with the map is unreadable at
		// the zoom where you most want to know which maze you are looking at. The lines scale,
		// the writing does not — which is how every map anybody has ever used behaves.
		//
		// Above the schematic, because that is where a caption belongs and because the space
		// below it is where the links run.
		const FVector2f LabelAt = CanvasToScreen(
			FVector2f(Node.Rect.Left, Node.Rect.Top)) - FVector2f(0.0f, LabelHeight);

		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(
				FVector2f((Node.Rect.Right - Node.Rect.Left) * Zoom, LabelHeight),
				FSlateLayoutTransform(LabelAt)),
			FText::FromString(Manifest->GetName()), Font, ESlateDrawEffect::None, LabelColor);
	}

	// --- the links, under the squares so the squares stay clickable-looking

	if (Live)
	{
		for (int32 LinkIndex = 0; LinkIndex < Live->Links.Num(); ++LinkIndex)
		{
			const FMazeWorldLink& Link = Live->Links[LinkIndex];

			FVector2f From;
			FVector2f To;

			const bool bHasFrom = ResolveEnd(Link.FromMaze, Link.FromId, From);
			const bool bHasTo = ResolveEnd(Link.ToMaze, Link.ToId, To);

			if (!bHasFrom || !bHasTo)
			{
				++BrokenLinkCount;

				// A stub out of whichever end still exists. There is nowhere to draw the other
				// half to — that point is gone — but a count without a picture leaves "which
				// three?" unanswered, and the answer is the whole reason to look at a map.
				const FVector2f Anchor = bHasFrom ? From : To;

				if (bHasFrom || bHasTo)
				{
					TArray<FVector2f> Stub;
					Stub.Add(Anchor);
					Stub.Add(Anchor + FVector2f(0.0f, -34.0f));

					FSlateDrawElement::MakeDashedLines(OutDrawElements, LayerId + 2, Geometry,
						MoveTemp(Stub), ESlateDrawEffect::None, BrokenColor, 2.0f, 5.0f);
				}

				continue;
			}

			const bool bSelected = LinkIndex == SelectedLink;
			const FLinearColor Color = bSelected ? SelectedColor : LinkColor;
			const float Thickness = bSelected ? 2.5f : 1.5f;

			AddSegment(OutDrawElements, LayerId + 2, Geometry, From, To, Color, Thickness);

			// An arrowhead, because a line between two squares says they are joined and not
			// which way anybody walks — and one-way is the whole point of a link.
			const FVector2f Direction = (To - From).GetSafeNormal();
			if (!Direction.IsNearlyZero())
			{
				const FVector2f Side(-Direction.Y, Direction.X);
				const FVector2f Tip = To - Direction * (PointHalf + 2.0f);

				AddSegment(OutDrawElements, LayerId + 2, Geometry, Tip,
					Tip - Direction * 9.0f + Side * 5.0f, Color, Thickness);
				AddSegment(OutDrawElements, LayerId + 2, Geometry, Tip,
					Tip - Direction * 9.0f - Side * 5.0f, Color, Thickness);
			}
		}
	}

	// --- the transition squares

	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const FMazeMapPoint& Point = Points[Index];
		const bool bGate = Point.Role == EMazeTransitionRole::Gate;
		const bool bPending = Index == PendingGate;

		const FLinearColor Color = bPending ? SelectedColor : (bGate ? GateColor : EntryColor);

		AddRect(OutDrawElements, LayerId + 3, Geometry,
			Point.Canvas - FVector2f(PointHalf, PointHalf),
			Point.Canvas + FVector2f(PointHalf, PointHalf), Color, bPending ? 3.0f : 2.0f);

		// The number is the only identity a point has, and it is what goes into the Links array
		// if anybody ever fills one in by hand. Cheap to draw, and it saves opening the manifest.
		//
		// Screen space again, and offset by a constant number of pixels rather than canvas
		// units: a label pushed aside by six canvas units sits on top of its own square when
		// zoomed out and half a screen away from it when zoomed in.
		const FVector2f NumberAt = CanvasToScreen(Point.Canvas)
			+ FVector2f(PointHalf * Zoom + 3.0f, -7.0f);

		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 4,
			AllottedGeometry.ToPaintGeometry(FVector2f(40.0f, 12.0f),
				FSlateLayoutTransform(NumberAt)),
			FText::AsNumber(Point.Id), Font, ESlateDrawEffect::None, Color);
	}

	return LayerId + 5;
}

bool SMazeWorldMapCanvas::ResolveEnd(const TSoftObjectPtr<UMazeWorldManifest>& Maze,
                                     const int32 Id, FVector2f& OutScreen) const
{
	const UMazeWorldManifest* Target = Maze.Get();
	if (!Target || Id == 0)
	{
		return false;
	}

	const FMazeMapPoint* Found = Points.FindByPredicate([&](const FMazeMapPoint& Point)
	{
		return Point.Id == Id && Nodes[Point.NodeIndex].Manifest.Get() == Target;
	});

	if (!Found)
	{
		return false;
	}

	OutScreen = Found->Canvas;
	return true;
}

bool SMazeWorldMapCanvas::ResolveLink(const int32 LinkIndex, FVector2f& OutFrom,
                                      FVector2f& OutTo) const
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live || !Live->Links.IsValidIndex(LinkIndex))
	{
		return false;
	}

	const FMazeWorldLink& Link = Live->Links[LinkIndex];

	return ResolveEnd(Link.FromMaze, Link.FromId, OutFrom)
		&& ResolveEnd(Link.ToMaze, Link.ToId, OutTo);
}

int32 SMazeWorldMapCanvas::HitTestPoint(const FVector2f& CanvasPos) const
{
	// The grab radius is divided by the zoom, so it stays the same number of PIXELS however far
	// in or out the view is. Left in canvas units it would be impossible to hit a square when
	// zoomed out and would swallow the whole maze when zoomed in.
	const float Grab = PointGrab / FMath::Max(Zoom, KINDA_SMALL_NUMBER);

	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if ((Points[Index].Canvas - CanvasPos).Size() <= Grab)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 SMazeWorldMapCanvas::HitTestNode(const FVector2f& CanvasPos) const
{
	for (int32 Index = Nodes.Num() - 1; Index >= 0; --Index)
	{
		const FSlateRect& Rect = Nodes[Index].Rect;

		// Grown upwards by the label, so the name is part of what can be grabbed: reaching for
		// the caption to drag a maze is the obvious thing to try.
		if (CanvasPos.X >= Rect.Left && CanvasPos.X <= Rect.Right
			&& CanvasPos.Y >= Rect.Top - LabelHeight && CanvasPos.Y <= Rect.Bottom)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 SMazeWorldMapCanvas::HitTestLink(const FVector2f& CanvasPos) const
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live)
	{
		return INDEX_NONE;
	}

	const float Grab = LinkGrab / FMath::Max(Zoom, KINDA_SMALL_NUMBER);

	for (int32 Index = 0; Index < Live->Links.Num(); ++Index)
	{
		FVector2f From;
		FVector2f To;

		if (ResolveLink(Index, From, To) && DistanceToSegment(CanvasPos, From, To) <= Grab)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void SMazeWorldMapCanvas::MakeLink(const int32 FromPoint, const int32 ToPoint)
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live || !Points.IsValidIndex(FromPoint) || !Points.IsValidIndex(ToPoint))
	{
		return;
	}

	const FMazeMapPoint& From = Points[FromPoint];
	const FMazeMapPoint& To = Points[ToPoint];

	UMazeWorldManifest* FromMaze = Nodes[From.NodeIndex].Manifest.Get();
	UMazeWorldManifest* ToMaze = Nodes[To.NodeIndex].Manifest.Get();

	if (!FromMaze || !ToMaze)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("MazeLinkMazes", "MazeForge: Link Mazes"));
	Live->Modify();

	// A gate leads to one place. Drawing a second line out of it is not two destinations, it is
	// somebody changing their mind — so the old link goes rather than quietly winning the
	// lookup, which takes the first match and would never mention the other.
	const int32 Existing = Live->Links.IndexOfByPredicate([&](const FMazeWorldLink& Candidate)
	{
		return Candidate.FromId == From.Id && Candidate.FromMaze.Get() == FromMaze;
	});

	if (Existing != INDEX_NONE)
	{
		Live->Links.RemoveAt(Existing);

		UE_LOG(LogMazeForge, Log,
			TEXT("World map: gate %d of %s already led somewhere. The old link was replaced."),
			From.Id, *FromMaze->GetName());
	}

	FMazeWorldLink Link;
	Link.FromMaze = FromMaze;
	Link.FromId = From.Id;
	Link.ToMaze = ToMaze;
	Link.ToId = To.Id;
	Live->Links.Add(Link);

	Live->MarkPackageDirty();
	Live->NotifyGraphChanged();

	UE_LOG(LogMazeForge, Log, TEXT("World map: %s gate %d now leads to %s entry %d."),
		*FromMaze->GetName(), From.Id, *ToMaze->GetName(), To.Id);
}

void SMazeWorldMapCanvas::DeleteSelectedLink()
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live || !Live->Links.IsValidIndex(SelectedLink))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("MazeUnlinkMazes", "MazeForge: Remove Link"));
	Live->Modify();

	Live->Links.RemoveAt(SelectedLink);
	SelectedLink = INDEX_NONE;

	Live->MarkPackageDirty();
	Live->NotifyGraphChanged();
}

namespace
{
	/**
	 *  Whether we can positively say this end of a link no longer exists.
	 *
	 *  The distinction is the whole safety of the button: a manifest that is not loaded yet
	 *  answers "I don't know", and not knowing must never count as gone.
	 */
	bool IsEndDefinitelyGone(const TSoftObjectPtr<UMazeWorldManifest>& Maze, const int32 Id)
	{
		const UMazeWorldManifest* Loaded = Maze.Get();
		return Loaded != nullptr && Loaded->FindTransition(Id) == nullptr;
	}

	bool IsLinkDefinitelyDead(const FMazeWorldLink& Link)
	{
		return IsEndDefinitelyGone(Link.FromMaze, Link.FromId)
			|| IsEndDefinitelyGone(Link.ToMaze, Link.ToId);
	}
}

int32 SMazeWorldMapCanvas::CountDeadLinks() const
{
	const UMazeWorldGraph* Live = Graph.Get();
	if (!Live)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FMazeWorldLink& Link : Live->Links)
	{
		Count += IsLinkDefinitelyDead(Link) ? 1 : 0;
	}

	return Count;
}

void SMazeWorldMapCanvas::RemoveDeadLinks()
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live)
	{
		return;
	}

	const FScopedTransaction Transaction(
		LOCTEXT("MazeRemoveDeadLinks", "MazeForge: Remove Dead Links"));
	Live->Modify();

	const int32 Removed = Live->Links.RemoveAll([](const FMazeWorldLink& Link)
	{
		return IsLinkDefinitelyDead(Link);
	});

	if (Removed > 0)
	{
		SelectedLink = INDEX_NONE;
		PendingGate = INDEX_NONE;

		Live->MarkPackageDirty();
		Live->NotifyGraphChanged();

		UE_LOG(LogMazeForge, Log,
			TEXT("World map: %d dead links removed. %d left."), Removed, Live->Links.Num());
	}
}

FReply SMazeWorldMapCanvas::OnMouseButtonDown(const FGeometry& MyGeometry,
                                              const FPointerEvent& MouseEvent)
{
	// Taken apart rather than converted: AbsoluteToLocal hands back a type that converts to
	// both the float and the double vector, and letting the compiler pick between them is how a
	// silent narrowing warning becomes an error on somebody else's machine.
	const FVector2D Absolute = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Local(static_cast<float>(Absolute.X), static_cast<float>(Absolute.Y));

	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton
		|| MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Both buttons, because neither is free of habit: middle-drag is the graph editor's
		// convention and right-drag is the viewport's, and a map is enough like each that
		// whichever one somebody reaches for should work.
		bPanning = true;
		PanLastScreen = Local;

		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	const FVector2f CanvasPos = ScreenToCanvas(Local);

	const int32 Hit = HitTestPoint(CanvasPos);

	if (Hit != INDEX_NONE)
	{
		SelectedLink = INDEX_NONE;

		if (Points[Hit].Role == EMazeTransitionRole::Gate)
		{
			// Clicking another gate while one is pending changes the mind rather than doing
			// nothing: the alternative is a click that silently achieves neither.
			PendingGate = (PendingGate == Hit) ? INDEX_NONE : Hit;
		}
		else if (PendingGate != INDEX_NONE)
		{
			MakeLink(PendingGate, Hit);
			PendingGate = INDEX_NONE;
		}
	}
	else
	{
		const int32 Link = HitTestLink(CanvasPos);

		if (Link != INDEX_NONE)
		{
			SelectedLink = Link;
			PendingGate = INDEX_NONE;
		}
		else
		{
			// Nothing precise under the cursor, so the schematic itself is fair game. Tested
			// after points and links on purpose: a maze covers both of them, and picking the
			// maze up whenever a square was meant would make the squares unclickable.
			const int32 Node = HitTestNode(CanvasPos);

			SelectedLink = INDEX_NONE;
			PendingGate = INDEX_NONE;

			if (Node != INDEX_NONE)
			{
				DraggingNode = Node;
				bDragMoved = false;
				DragGrabOffset = CanvasPos
					- FVector2f(Nodes[Node].Rect.Left, Nodes[Node].Rect.Top);

				OnSelectionChanged.ExecuteIfBound();

				return FReply::Handled()
					.CaptureMouse(SharedThis(this))
					.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
			}
		}
	}

	OnSelectionChanged.ExecuteIfBound();

	return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SMazeWorldMapCanvas::OnMouseButtonUp(const FGeometry& MyGeometry,
                                            const FPointerEvent& MouseEvent)
{
	if (bPanning
		&& (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton
			|| MouseEvent.GetEffectingButton() == EKeys::RightMouseButton))
	{
		bPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	if (DraggingNode != INDEX_NONE
		&& MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		UMazeWorldGraph* Live = Graph.Get();

		// The transaction is taken here and not on every mouse move: a drag is one thing the
		// designer did, and an undo history with a step per frame is an undo history nobody
		// can walk back. The position has been live on the graph all along; this is what makes
		// it permanent and undoable.
		if (bDragMoved && Live && Nodes.IsValidIndex(DraggingNode))
		{
			if (const UMazeWorldManifest* Manifest = Nodes[DraggingNode].Manifest.Get())
			{
				const FScopedTransaction Transaction(
					LOCTEXT("MazeMoveMaze", "MazeForge: Move Maze On World Map"));

				Live->Modify();
				Live->SetLayout(Manifest, FVector2D(Nodes[DraggingNode].Rect.Left,
				                                    Nodes[DraggingNode].Rect.Top));
				Live->MarkPackageDirty();
				Live->NotifyGraphChanged();
			}
		}

		DraggingNode = INDEX_NONE;
		bDragMoved = false;

		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

FReply SMazeWorldMapCanvas::OnMouseMove(const FGeometry& MyGeometry,
                                        const FPointerEvent& MouseEvent)
{
	if (!bPanning && DraggingNode == INDEX_NONE)
	{
		return FReply::Unhandled();
	}

	const FVector2D Absolute = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Local(static_cast<float>(Absolute.X), static_cast<float>(Absolute.Y));

	if (DraggingNode != INDEX_NONE)
	{
		UMazeWorldGraph* Live = Graph.Get();

		if (Live && Nodes.IsValidIndex(DraggingNode))
		{
			if (const UMazeWorldManifest* Manifest = Nodes[DraggingNode].Manifest.Get())
			{
				// Written straight to the graph rather than kept in the widget, because the
				// layout is rebuilt from the graph on every single paint. A drag held in a
				// local variable would be undone by the next frame.
				const FVector2f Target = ScreenToCanvas(Local) - DragGrabOffset;

				Live->SetLayout(Manifest, FVector2D(Target.X, Target.Y));
				bDragMoved = true;
			}
		}

		return FReply::Handled();
	}

	// Divided by the zoom, so the canvas keeps up with the cursor exactly: the point under the
	// mouse when the drag started stays under it. Panning that drifts is panning that fights.
	ViewOffset -= (Local - PanLastScreen) / FMath::Max(Zoom, KINDA_SMALL_NUMBER);
	PanLastScreen = Local;

	ClampView();

	return FReply::Handled();
}

FReply SMazeWorldMapCanvas::OnMouseWheel(const FGeometry& MyGeometry,
                                         const FPointerEvent& MouseEvent)
{
	const float Delta = MouseEvent.GetWheelDelta();
	if (FMath::IsNearlyZero(Delta))
	{
		return FReply::Unhandled();
	}

	const FVector2D Absolute = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Local(static_cast<float>(Absolute.X), static_cast<float>(Absolute.Y));

	if (MouseEvent.IsShiftDown() || MouseEvent.IsControlDown())
	{
		// Scrolling with a modifier, the way a document does. Shift for sideways is universal;
		// Ctrl is here because half the editors in the world use it for the other axis.
		const float Amount = 60.0f * Delta / FMath::Max(Zoom, KINDA_SMALL_NUMBER);

		if (MouseEvent.IsShiftDown())
		{
			ViewOffset.X -= Amount;
		}
		else
		{
			ViewOffset.Y -= Amount;
		}

		ClampView();
		return FReply::Handled();
	}

	// Zoom towards the cursor: the canvas point under the mouse is found first and put back
	// under the mouse afterwards. Zooming towards the centre instead makes reaching a corner a
	// matter of alternating zoom and pan, which is how a map earns a reputation for fighting.
	const FVector2f Anchor = ScreenToCanvas(Local);

	Zoom = FMath::Clamp(Zoom * FMath::Pow(ZoomStep, Delta), MinZoom, MaxZoom);
	ViewOffset = Anchor - Local / FMath::Max(Zoom, KINDA_SMALL_NUMBER);

	ClampView();

	return FReply::Handled();
}

FCursorReply SMazeWorldMapCanvas::OnCursorQuery(const FGeometry& MyGeometry,
                                                const FPointerEvent& CursorEvent) const
{
	if (bPanning || DraggingNode != INDEX_NONE)
	{
		return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	}

	// A hand over a schematic says it can be picked up. Without it dragging a maze is a feature
	// nobody discovers, because nothing on screen suggests it is there.
	const FVector2D Absolute = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
	const FVector2f CanvasPos = ScreenToCanvas(
		FVector2f(static_cast<float>(Absolute.X), static_cast<float>(Absolute.Y)));

	if (HitTestPoint(CanvasPos) == INDEX_NONE && HitTestNode(CanvasPos) != INDEX_NONE)
	{
		return FCursorReply::Cursor(EMouseCursor::GrabHand);
	}

	return FCursorReply::Unhandled();
}

FReply SMazeWorldMapCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Delete || KeyEvent.GetKey() == EKeys::BackSpace)
	{
		DeleteSelectedLink();
		OnSelectionChanged.ExecuteIfBound();
		return FReply::Handled();
	}

	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		PendingGate = INDEX_NONE;
		SelectedLink = INDEX_NONE;
		OnSelectionChanged.ExecuteIfBound();
		return FReply::Handled();
	}

	// F for frame, the same key the level viewport uses for the same idea. The way back when
	// the view has been scrolled somewhere empty and the map looks broken.
	if (KeyEvent.GetKey() == EKeys::F || KeyEvent.GetKey() == EKeys::Home)
	{
		ZoomToFit();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FText SMazeWorldMapCanvas::GetStatusText() const
{
	UMazeWorldGraph* Live = Graph.Get();
	if (!Live)
	{
		return LOCTEXT("MapNoGraph", "Pick a Maze World Graph above.");
	}

	if (Nodes.Num() == 0)
	{
		// Two different empties, and they used to read as one. A graph nobody has filled in
		// and a graph full of mazes that have never been built need different things done
		// about them, and a single "no mazes to draw" sent the last person looking in the
		// wrong place.
		if (Live->Mazes.Num() == 0)
		{
			return LOCTEXT("MapNoMazes",
				"This world graph names no mazes. Add their manifests with the picker above.");
		}

		return FText::Format(
			LOCTEXT("MapNoBuiltMazes",
				"{0} mazes are named, but none of them has anything to draw. A maze appears "
				"here once it has been built, because the map is drawn from the manifest — "
				"press Apply Changes on each of them."),
			FText::AsNumber(Live->Mazes.Num()));
	}

	if (PendingGate != INDEX_NONE && Points.IsValidIndex(PendingGate))
	{
		return FText::Format(
			LOCTEXT("MapPending", "Gate {0} is waiting — click a red square to say where it leads. "
			                      "Escape cancels."),
			FText::AsNumber(Points[PendingGate].Id));
	}

	if (SelectedLink != INDEX_NONE && Live->Links.IsValidIndex(SelectedLink))
	{
		const FMazeWorldLink& Link = Live->Links[SelectedLink];

		return FText::Format(
			LOCTEXT("MapSelected", "Link {0} → {1} selected. Delete removes it."),
			FText::AsNumber(Link.FromId), FText::AsNumber(Link.ToId));
	}

	if (BrokenLinkCount > 0)
	{
		// Said out loud and not merely drawn dashed, because this is the failure the id-as-key
		// design buys: delete a point and draw it again and it is a new point with a new number.
		return FText::Format(
			LOCTEXT("MapBroken",
				"{0} of {1} links lead nowhere — one of their ends no longer exists. Usually the "
				"point was deleted and drawn again, which gives it a new number. Draw them again."),
			FText::AsNumber(BrokenLinkCount), FText::AsNumber(Live->Links.Num()));
	}

	return FText::Format(
		LOCTEXT("MapIdle",
			"{0} mazes, {1} links, {2}%. Click a blue gate, then a red entry. "
			"Wheel zooms, middle or right drag pans, F frames everything."),
		FText::AsNumber(Nodes.Num()), FText::AsNumber(Live->Links.Num()),
		FText::AsNumber(FMath::RoundToInt(Zoom * 100.0f)));
}

// ===================================================================== the window

void SMazeWorldMap::Construct(const FArguments& InArgs)
{
	// Picked up before the widgets are built, so the picker and the canvas come up already
	// pointing at it rather than flickering through empty on the first frame.
	if (PendingGraph.IsValid())
	{
		Graph = PendingGraph;
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("MapGraphLabel", "World Graph"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UMazeWorldGraph::StaticClass())
				.ObjectPath(this, &SMazeWorldMap::GetGraphPath)
				.OnObjectChanged(this, &SMazeWorldMap::OnGraphPicked)
				.AllowClear(true)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("MapAddLabel", "Add maze"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UMazeWorldManifest::StaticClass())
				.ObjectPath_Lambda([]() { return FString(); })
				.OnObjectChanged(this, &SMazeWorldMap::OnManifestPicked)
				.AllowClear(false)
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			// The canvas with a bar down its right and another along its bottom, the way any
			// window onto something larger than itself is arranged. The bars are driven from
			// the canvas rather than driving it: the wheel and the drag move the view too, and
			// two things both claiming to own the scroll position is how they end up disagreeing.
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBorder)
					.Padding(0.0f)
					[
						SAssignNew(Canvas, SMazeWorldMapCanvas)
						.Graph_Lambda([this]() { return Graph.Get(); })
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(HorizontalBar, SScrollBar)
					.Orientation(Orient_Horizontal)
					.AlwaysShowScrollbar(true)
					.OnUserScrolled_Lambda([this](float Fraction)
					{
						if (Canvas.IsValid())
						{
							Canvas->SetScrollOffsetFraction(Orient_Horizontal, Fraction);
						}
					})
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(VerticalBar, SScrollBar)
				.Orientation(Orient_Vertical)
				.AlwaysShowScrollbar(true)
				.OnUserScrolled_Lambda([this](float Fraction)
				{
					if (Canvas.IsValid())
					{
						Canvas->SetScrollOffsetFraction(Orient_Vertical, Fraction);
					}
				})
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f, 8.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("MazeFitTip",
					"Frames every maze in the graph. The way back when the view has been "
					"scrolled somewhere empty and the map looks broken. Also the F key."))
				.OnClicked_Lambda([this]()
				{
					if (Canvas.IsValid())
					{
						Canvas->ZoomToFit();
					}

					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(LOCTEXT("MazeFit", "Fit"))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.ToolTipText(LOCTEXT("MazeRemoveDeadTip",
				"Removes the links whose ends no longer exist. A link into a maze that is not "
				"loaded is left alone — not being able to see it is not the same as it being gone."))
			.IsEnabled_Lambda([this]()
			{
				return Canvas.IsValid() && Canvas->CountDeadLinks() > 0;
			})
			.OnClicked_Lambda([this]()
			{
				if (Canvas.IsValid())
				{
					Canvas->RemoveDeadLinks();
				}

				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const int32 Dead = Canvas.IsValid() ? Canvas->CountDeadLinks() : 0;

					return Dead > 0
						? FText::Format(LOCTEXT("MazeRemoveDeadN", "Remove {0} dead links"),
							FText::AsNumber(Dead))
						: LOCTEXT("MazeRemoveDeadNone", "No dead links");
				})
			]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f)
		[
			SAssignNew(Status, STextBlock)
			.AutoWrapText(true)
			.Text_Lambda([this]()
			{
				return Canvas.IsValid() ? Canvas->GetStatusText() : FText::GetEmpty();
			})
		]
	];
}

void SMazeWorldMap::Tick(const FGeometry& AllottedGeometry, const double CurrentTime,
                         const float DeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, CurrentTime, DeltaTime);

	// Registered here rather than in Construct: SharedThis is not available until the shared
	// pointer to this widget exists, which it does not while the constructor is still running.
	if (!LiveInstance.IsValid())
	{
		LiveInstance = SharedThis(this);
	}

	if (!Canvas.IsValid())
	{
		return;
	}

	// Pushed to the bars every frame rather than the bars being asked. The view moves for three
	// different reasons — a drag, the wheel, a zoom-to-fit — and only one of them goes through
	// the bars; anything else leaves a thumb sitting where the view no longer is.
	float Offset = 0.0f;
	float Thumb = 1.0f;

	if (HorizontalBar.IsValid())
	{
		Canvas->GetScrollState(Orient_Horizontal, Offset, Thumb);
		HorizontalBar->SetState(Offset, Thumb);
	}

	if (VerticalBar.IsValid())
	{
		Canvas->GetScrollState(Orient_Vertical, Offset, Thumb);
		VerticalBar->SetState(Offset, Thumb);
	}
}

void SMazeWorldMap::SetGraphToShow(UMazeWorldGraph* InGraph)
{
	PendingGraph = InGraph;

	// Applied straight away when there is a window, because the usual case is pressing the
	// button a second time after changing the field — and a window that ignored that would look
	// like the button had stopped working.
	if (const TSharedPtr<SMazeWorldMap> Live = LiveInstance.Pin())
	{
		Live->Graph = InGraph;

		// Framed on the new graph, because the view left over from the previous one is almost
		// certainly looking at empty canvas now.
		if (Live->Canvas.IsValid())
		{
			Live->Canvas->ZoomToFit();
		}
	}
}

FString SMazeWorldMap::GetGraphPath() const
{
	const UMazeWorldGraph* Live = Graph.Get();
	return Live ? Live->GetPathName() : FString();
}

void SMazeWorldMap::OnGraphPicked(const FAssetData& AssetData)
{
	Graph = Cast<UMazeWorldGraph>(AssetData.GetAsset());
}

void SMazeWorldMap::OnManifestPicked(const FAssetData& AssetData)
{
	UMazeWorldGraph* Live = Graph.Get();
	UMazeWorldManifest* Manifest = Cast<UMazeWorldManifest>(AssetData.GetAsset());

	if (!Live || !Manifest)
	{
		return;
	}

	if (Live->HasMaze(Manifest))
	{
		UE_LOG(LogMazeForge, Log, TEXT("World map: %s is already on the map."),
			*Manifest->GetName());
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("MazeAddMazeToMap", "MazeForge: Add Maze To Map"));
	Live->Modify();

	Live->Mazes.Add(Manifest);
	Live->MarkPackageDirty();
	Live->NotifyGraphChanged();
}

#undef LOCTEXT_NAMESPACE
