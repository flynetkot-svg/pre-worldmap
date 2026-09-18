#pragma once

#include "CoreMinimal.h"
#include "Data/MazeSpawnTypes.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBar;
class STextBlock;
class UMazeWorldGraph;
class UMazeWorldManifest;

/** One maze laid out on the map. */
struct FMazeMapNode
{
	TWeakObjectPtr<UMazeWorldManifest> Manifest;

	/**
	 *  Where its schematic sits in canvas space.
	 *
	 *  Canvas space, not widget space, and that distinction is what makes zooming possible at
	 *  all. The layout used to be measured against the width of the window, so a resize
	 *  re-wrapped the rows — and a zoom, which changes how much window a maze covers, would
	 *  have re-wrapped them with every notch of the wheel. Mazes now sit at fixed places and
	 *  the view moves over them.
	 */
	FSlateRect Rect;

	/** The maze's own bounds, X and Z. Min is (X, Z), Max is (X, Z) — Y is not a thing here. */
	FVector2f WorldMin = FVector2f::ZeroVector;
	FVector2f WorldMax = FVector2f::ZeroVector;
};

/** A transition point, placed on the canvas and ready to be clicked. */
struct FMazeMapPoint
{
	int32 NodeIndex = INDEX_NONE;
	int32 Id = 0;
	EMazeTransitionRole Role = EMazeTransitionRole::None;

	/** Canvas space, like everything else laid out. */
	FVector2f Canvas = FVector2f::ZeroVector;
};

/**
 *  The drawing half of the world map.
 *
 *  Everything it draws comes out of the manifests: the room rectangles are the schematic, the
 *  transition points are the squares. Nothing loads a grid asset — a grid is megabytes of cells
 *  and the picture needs none of them, which is what makes a map of ten mazes cheap.
 */
class SMazeWorldMapCanvas : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMazeWorldMapCanvas) {}
		SLATE_ATTRIBUTE(UMazeWorldGraph*, Graph)
		SLATE_EVENT(FSimpleDelegate, OnSelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	//~ SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	                      const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements,
	                      int32 LayerId, const FWidgetStyle& WidgetStyle,
	                      bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry,
	                                 const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry,
	                               const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry,
	                           const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry,
	                            const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry,
	                                   const FPointerEvent& CursorEvent) const override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(900.0, 420.0); }

	/** What the status line under the canvas says. Recomputed on demand, not cached. */
	FText GetStatusText() const;

	// ------------------------------------------------------------------------ the view

	/** Current magnification. One is canvas units to screen pixels. */
	float GetZoom() const { return Zoom; }

	/** Frames everything the graph draws, and centres it. Also what the F key does. */
	void ZoomToFit();

	/**
	 *  Where the scroll bar's thumb goes and how big it is, as fractions of the whole.
	 *
	 *  Both in one call because the bar needs both at once and they come from the same two
	 *  rectangles: what there is to look at, and how much of it fits.
	 */
	void GetScrollState(EOrientation Orientation, float& OutOffsetFraction,
	                    float& OutThumbFraction) const;

	/** Moves the view so the thumb lands at this fraction. The bars call it. */
	void SetScrollOffsetFraction(EOrientation Orientation, float Fraction);

private:
	/** Lays the mazes out in rows and places every point. Called from OnPaint. */
	void RebuildLayout() const;

	/** World XZ to canvas space, inside one maze's rectangle. */
	static FVector2f ToCanvas(const FMazeMapNode& Node, const FVector& World);

	/** Canvas space to widget space, and back. The whole of the view is these two. */
	FVector2f CanvasToScreen(const FVector2f& CanvasPos) const;
	FVector2f ScreenToCanvas(const FVector2f& ScreenPos) const;

	/** Everything laid out, in canvas space, padded a little. Empty graph gives a unit square. */
	FSlateRect CanvasExtent() const;

	/** Keeps the view from being scrolled somewhere with nothing in it. */
	void ClampView();

	/** Draws the background rulers. Canvas-aligned, so they say what the scale is. */
	void PaintGrid(FSlateWindowElementList& OutDrawElements, int32 LayerId,
	               const FGeometry& AllottedGeometry, const FPaintGeometry& Geometry) const;

	/** The point under this canvas position, or INDEX_NONE. */
	int32 HitTestPoint(const FVector2f& CanvasPos) const;

	/**
	 *  The maze whose schematic covers this position, or INDEX_NONE.
	 *
	 *  Searched back to front so the one drawn last — the one on top where two overlap — is the
	 *  one that gets picked up, which is what dragging one out of a pile has to do.
	 */
	int32 HitTestNode(const FVector2f& CanvasPos) const;

	/** The link whose line passes under this position, or INDEX_NONE. */
	int32 HitTestLink(const FVector2f& CanvasPos) const;

	/** Screen position of one end of a link. False when that point no longer exists. */
	bool ResolveEnd(const TSoftObjectPtr<UMazeWorldManifest>& Maze, int32 Id,
	                FVector2f& OutScreen) const;

	/** Both ends. False when either is missing — which is what a broken link is. */
	bool ResolveLink(int32 LinkIndex, FVector2f& OutFrom, FVector2f& OutTo) const;

	void MakeLink(int32 FromPoint, int32 ToPoint);
	void DeleteSelectedLink();

public:
	/**
	 *  How many links can be proved dead right now.
	 *
	 *  Proved, not merely undrawable: a link into a maze whose manifest is not loaded cannot be
	 *  drawn either, and deleting those would quietly destroy the graph of anybody who opened
	 *  the window before opening their mazes.
	 */
	int32 CountDeadLinks() const;

	/** Removes exactly those. Undoable. */
	void RemoveDeadLinks();

private:

	TAttribute<UMazeWorldGraph*> Graph;
	FSimpleDelegate OnSelectionChanged;

	// Laid out in OnPaint and read by the mouse handlers, which run against the same geometry.
	mutable TArray<FMazeMapNode> Nodes;
	mutable TArray<FMazeMapPoint> Points;

	/** The gate waiting for its destination, as an index into Points. */
	int32 PendingGate = INDEX_NONE;

	/** The selected link, as an index into the graph's Links. Delete removes it. */
	int32 SelectedLink = INDEX_NONE;

	/** Filled during paint: links whose ends no longer exist. Drawn dashed and counted. */
	mutable int32 BrokenLinkCount = 0;

	// ------------------------------------------------------------------------ the view

	/**
	 *  Canvas units per screen pixel, and where the top-left of the window sits on the canvas.
	 *
	 *  Two numbers rather than a matrix because that is all a 2D map needs, and because both
	 *  have to be reachable by the scroll bars, the wheel, and the clamp that stops the view
	 *  from wandering off into empty canvas.
	 */
	float Zoom = 1.0f;
	FVector2f ViewOffset = FVector2f::ZeroVector;

	/** Set in OnPaint so the mouse handlers know what the view was last drawn against. */
	mutable FVector2f LastViewportSize = FVector2f(900.0f, 420.0f);

	/** True while the middle or right button drags the view. */
	bool bPanning = false;
	FVector2f PanLastScreen = FVector2f::ZeroVector;

	/**
	 *  The maze being dragged, as an index into Nodes, and where it was grabbed.
	 *
	 *  The grab offset is what keeps the schematic from jumping so that its corner snaps to the
	 *  cursor the moment the drag starts. It is in canvas units, so zooming mid-drag does not
	 *  move the maze under the hand.
	 */
	int32 DraggingNode = INDEX_NONE;
	FVector2f DragGrabOffset = FVector2f::ZeroVector;

	/** Set once the drag has actually moved something, so a plain click writes nothing. */
	bool bDragMoved = false;

	/** Set once the first layout is known, so an empty map opens framed rather than at 1:1. */
	mutable bool bViewInitialised = false;
};

/** The window: pick a world graph at the top, draw it below, say what is going on underneath. */
class SMazeWorldMap : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMazeWorldMap) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Keeps the scroll bars showing where the view actually is. */
	virtual void Tick(const FGeometry& AllottedGeometry, double CurrentTime,
	                  float DeltaTime) override;

	/** The tab id the editor module registers this under. */
	static const FName TabId;

	/**
	 *  Hands the window a graph to show, from outside.
	 *
	 *  What Open World Map calls, so the button carries the graph already named in the mode
	 *  panel instead of opening onto an empty picker and asking for it a second time. Applied
	 *  at once when the window is already open, remembered for its Construct when it is not.
	 */
	static void SetGraphToShow(UMazeWorldGraph* InGraph);

private:
	FString GetGraphPath() const;
	void OnGraphPicked(const FAssetData& AssetData);

	/**
	 *  The one open window, and the graph waiting for it.
	 *
	 *  Static because the caller is a button on a settings object with no way to reach a Slate
	 *  widget, and the tab manager hands back a tab rather than the thing inside it. One window
	 *  because that is what the tab manager allows — invoking the tab twice focuses the first.
	 */
	static TWeakPtr<SMazeWorldMap> LiveInstance;
	static TWeakObjectPtr<UMazeWorldGraph> PendingGraph;

	/** Adds a maze to the graph from the asset picker. */
	void OnManifestPicked(const FAssetData& AssetData);

	TWeakObjectPtr<UMazeWorldGraph> Graph;
	TSharedPtr<SMazeWorldMapCanvas> Canvas;
	TSharedPtr<STextBlock> Status;

	/**
	 *  Driven from the canvas, never the other way round.
	 *
	 *  The view moves for three reasons and only one of them is a bar being dragged, so the
	 *  bars are told where things are once a frame rather than being asked to remember.
	 */
	TSharedPtr<SScrollBar> HorizontalBar;
	TSharedPtr<SScrollBar> VerticalBar;
};
