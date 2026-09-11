#pragma once

#include "CoreMinimal.h"
#include "Data/MazeRoomDesc.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"
#include "MazeEdMode.generated.h"

class AMazePreviewActor;
class UMazeEdModeSettings;
class UMazeGridAsset;
class ULevel;
class UWorld;
class FSceneView;
struct FMazeGrid;

/**
 *  Manual maze drawing mode.
 *
 *  We derive from UBaseLegacyWidgetEdMode rather than from a bare UEdMode: it is the one that
 *  mixes in ILegacyEdModeWidgetInterface (Render/DrawHUD) and
 *  ILegacyEdModeViewportInterface (MouseMove/InputKey/Tracking). A plain
 *  UEdMode does not have those entry points at all.
 *
 *  Controls:
 *      LMB                 place a block
 *      Shift + LMB         erase
 *      Ctrl + LMB + drag   rectangle fill (the main time saver)
 *      Ctrl + Shift + drag rectangle erase
 *      PgUp / PgDn         active depth slice
 *      [ / ]               brush size
 */
UCLASS()
class MAZEFORGEEDITOR_API UMazeEdMode : public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()

public:
	static const FEditorModeID EM_MazeForge;

	UMazeEdMode();

	// --- UEdMode
	virtual void Initialize() override;
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void CreateToolkit() override;
	virtual bool UsesToolkits() const override { return true; }

	// --- ILegacyEdModeWidgetInterface
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport,
	                     const FSceneView* View, FCanvas* Canvas) override;
	virtual bool ShouldDrawWidget() const override { return false; }
	virtual bool UsesTransformWidget() const override { return false; }

	// --- ILegacyEdModeViewportInterface
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 X, int32 Y) override;
	virtual bool CapturedMouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 X, int32 Y) override;
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool StartTracking(FEditorViewportClient* ViewportClient, FViewport* Viewport) override;
	virtual bool EndTracking(FEditorViewportClient* ViewportClient, FViewport* Viewport) override;
	virtual bool DisallowMouseDeltaTracking() const override;
	virtual bool AllowsViewportDragTool() const override { return false; }

	UMazeEdModeSettings* GetSettings() const { return Settings; }
	UMazeGridAsset* GetTargetAsset() const;

	void RebuildPreview();

	/** Asks every editor viewport for a new frame. A component change alone does not. */
	static void InvalidateViewports();

private:
	/** Rooms whose levels are currently loaded in the editor — the preview does not duplicate them. */
	void GatherExportedRoomIds(TSet<FName>& OutRoomIds) const;

public:

private:
	UPROPERTY()
	TObjectPtr<UMazeEdModeSettings> Settings;

	UPROPERTY()
	TObjectPtr<AMazePreviewActor> Preview;

	bool bHasHover = false;
	FIntVector HoveredCell = FIntVector::ZeroValue;

	/** The camera is looking along the drawing plane: drawing in that view is impossible in principle. */
	bool bViewParallelToPlane = false;

	/** The peek key is held down: isolation is suspended. Transient, never saved. */
	bool bPeeking = false;

	/**
	 *  Why the cursor found no cell, when it found none. Shown in the overlay.
	 *
	 *  Silence here cost several rounds of guessing: "I cannot draw" and "the brush lands in the
	 *  wrong slice" and "the view is side-on" all look identical from outside. They do not have to.
	 */
	FString NoHoverReason;

	/** Ends any stroke left open and closes its transaction. Safe to call when none is open. */
	void AbortStroke();

	/**
	 *  Places or erases one object at the hovered cell. Returns true if anything changed.
	 *
	 *  A single click and not a stroke: objects are placed one at a time, and dragging a crate
	 *  brush across the map would bury the level in crates faster than anyone could undo it.
	 */
	bool ApplyObjectClick(bool bErase);

	/** The object brush's verdict for the hovered cell, ready to append to the HUD line. */
	FString ObjectHoverStatus(const UMazeGridAsset& Asset) const;

	/** How many rooms the last rebuild hid. Kept only so the log reports changes, not every frame. */
	int32 LastSuppressedRoomCount = -1;

	/**
	 *  The slicing that Preview Rooms shows: computed from the slicer's settings and stored
	 *  nowhere else. It is recomputed whenever the grid or the settings change, not per frame —
	 *  the slicer counts every room's contents and builds the portal graph, which is far too much
	 *  work to repeat while drawing.
	 */
	TArray<FMazeRoomDesc> PreviewRooms;

	void RebuildRoomPreview();

	/**
	 *  Creates the preview actor if there is not a live one, and puts it in the persistent level.
	 *
	 *  Both halves matter. SpawnActor without an override drops the actor into the world's
	 *  CURRENT level, which after Attach Rooms To Level is a room level — and detaching that room
	 *  destroys the preview along with it. And a destroyed actor leaves a pointer that still
	 *  tests non-null, so nothing notices until the viewport is empty for no reason at all.
	 */
	void EnsurePreviewActor();

	/** Instance count of the last rebuild. Logged only when it changes. */
	int32 LastPreviewInstanceCount = -1;

	/**
	 *  The preview hides rooms whose levels are open in the editor, so it has to be told when a
	 *  level comes or goes.
	 *
	 *  Detach Rooms From Level used to leave the mode showing nothing: the levels were gone but
	 *  the preview still thought their geometry was on screen, so it drew none of it. Switching
	 *  modes and back brought part of it back, and touching any setting brought the rest —
	 *  three symptoms of one missing signal.
	 */
	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelRemovedHandle;

	void OnLevelChangedInWorld(ULevel* Level, UWorld* World);

	/**
	 *  Drop the peek if the key is no longer physically down.
	 *
	 *  A key release is not guaranteed to arrive: hold the key, alt-tab away, let go outside the
	 *  editor, and the mode would sit in peek for ever. Called from mouse movement, so the state
	 *  repairs itself the moment the cursor comes back over the viewport.
	 */
	void ReconcilePeek(FViewport* Viewport);

	bool bPainting = false;
	bool bErasing = false;
	bool bBoxDrag = false;
	FIntVector DragStartCell = FIntVector::ZeroValue;

	/** Intersection of the cursor ray with the plane of the active depth slice. */
	bool ComputeCellUnderCursor(FEditorViewportClient* ViewportClient, FIntVector& OutCell);

	/**
	 *  Where the centre of the viewport looks on the slice plane. Needed so that the grid overlay
	 *  is drawn where the designer is looking, not at the geometric centre of the maze.
	 */
	bool ProjectViewCentreToCell(const FSceneView* View, const FMazeGrid& Grid, FIntVector& OutCell) const;

	/**
	 *  The range of cells that actually fall inside the frame: deprojection of the four viewport
	 *  corners onto the plane of the active slice. The grid overlay must cover the whole screen
	 *  instead of breaking off at a fixed window in the middle of it.
	 */
	bool ComputeVisibleCellRange(const FSceneView* View, const FMazeGrid& Grid,
	                             FIntPoint& OutMinXZ, FIntPoint& OutMaxXZ) const;

	void BeginStroke(bool bInErase, bool bInBox);
	void ContinueStroke();
	void EndStroke();

	int32 ApplyBrushAt(const FIntVector& Centre, bool bErase);
	int32 ApplyBox(const FIntVector& A, const FIntVector& B, bool bErase);

	/** The range of cells along Y the stroke lands on, from the current DepthApply. */
	void GetDepthRange(const FMazeGrid& Grid, int32& OutMinY, int32& OutMaxY) const;

	void OnAssetGridChanged();
	void OnSettingsChanged();
	void BindAsset(UMazeGridAsset* Asset);

	TWeakObjectPtr<UMazeGridAsset> BoundAsset;
	FDelegateHandle GridChangedHandle;
	int32 StrokeChangedCells = 0;

	/** Time of the last preview rebuild. Used to throttle rebuilds during a stroke. */
	double LastPreviewRebuildTime = 0.0;
};
