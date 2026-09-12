#pragma once

#include "CoreMinimal.h"
#include "Data/MazeSpawnTypes.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
class UMazeWorldGraph;
class UMazeWorldManifest;

/** One maze laid out on the map. */
struct FMazeMapNode
{
	TWeakObjectPtr<UMazeWorldManifest> Manifest;

	/** Where its schematic sits on the canvas, in local space. */
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
	FVector2f Screen = FVector2f::ZeroVector;
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
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(900.0, 420.0); }

	/** What the status line under the canvas says. Recomputed on demand, not cached. */
	FText GetStatusText() const;

private:
	/** Lays the mazes out in rows and places every point. Called from OnPaint. */
	void RebuildLayout(const FGeometry& Geometry) const;

	/** World XZ to canvas space, inside one maze's rectangle. */
	static FVector2f ToLocal(const FMazeMapNode& Node, const FVector& World);

	/** The point under this canvas position, or INDEX_NONE. */
	int32 HitTestPoint(const FVector2f& Local) const;

	/** The link whose line passes under this position, or INDEX_NONE. */
	int32 HitTestLink(const FVector2f& Local) const;

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
};

/** The window: pick a world graph at the top, draw it below, say what is going on underneath. */
class SMazeWorldMap : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMazeWorldMap) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The tab id the editor module registers this under. */
	static const FName TabId;

private:
	FString GetGraphPath() const;
	void OnGraphPicked(const FAssetData& AssetData);

	/** Adds a maze to the graph from the asset picker. */
	void OnManifestPicked(const FAssetData& AssetData);

	TWeakObjectPtr<UMazeWorldGraph> Graph;
	TSharedPtr<SMazeWorldMapCanvas> Canvas;
	TSharedPtr<STextBlock> Status;
};
