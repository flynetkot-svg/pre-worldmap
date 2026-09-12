#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/Object.h"
#include "Data/MazeTypes.h"
#include "Data/MazeSpawnTypes.h"
#include "MazeEdModeSettings.generated.h"

class UMazeEditorStyleAsset;
class UMazeGridAsset;
class UMazeObjectLibrary;
class UMazeSpawnAsset;

/** What the mouse does in the viewport. */
UENUM()
enum class EMazeEditTool : uint8
{
	/** Paint the mass of the maze: Solid, Floor, BackWall. */
	Cells   = 0,
	/** Place objects from the library. */
	Objects = 1
};

/** How a brush stroke is spread through the depth axis Y. */
UENUM()
enum class EMazeDepthApply : uint8
{
	/** Follows the bFill* flags of the depth profile: usually BACKGROUND + PLAY. The main mode. */
	FillBands = 0,
	/** The active band only — a pinpoint edit of a single slice. */
	ActiveSliceOnly = 1,
	/** The full depth, foreground included. */
	AllDepth = 2,
	/** The play band only. */
	PlayBandOnly = 3
};

/**
 *  Mode settings. The panel is built from these UPROPERTY fields automatically.
 *
 *  The class is marked config: the chosen maze, the style preset and the brush settings
 *  survive an editor restart and live in EditorPerProjectUserSettings.ini.
 *  Without that, every launch would start with picking the target again — the settings
 *  object is created in UMazeEdMode::Enter and is never saved in the project itself.
 */
UCLASS(config = EditorPerProjectUserSettings)
class MAZEFORGEEDITOR_API UMazeEdModeSettings : public UObject
{
	GENERATED_BODY()

public:
	/**
	 *  The maze being edited. Without it the mode draws nothing.
	 *
	 *  This is deliberately a TSoftObjectPtr and not a TObjectPtr: UHT forbids config on object
	 *  pointers (UhtObjectPropertyBase clears CanHaveConfig), while soft references get that
	 *  ability back explicitly. Without config the chosen target would be lost on every
	 *  editor restart.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Target")
	TSoftObjectPtr<UMazeGridAsset> TargetAsset;

	/**
	 *  The state of the target in a single line: plane or volume, cells, rooms, snapshot.
	 *
	 *  So you do not have to open the asset just to ask "what shape is it in right now?".
	 *  Updated after every button and whenever the target changes.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Target")
	FString Status;

	/**
	 *  Which brush the mouse is holding.
	 *
	 *  One tool at a time rather than a modifier key, because the two draw different things into
	 *  different containers: cells go into the grid, objects into the spawn asset. A modifier
	 *  that silently changed which file a click edits is a good way to lose work.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Brush")
	EMazeEditTool Tool = EMazeEditTool::Cells;

	UPROPERTY(EditAnywhere, config, Category = "Brush",
		meta = (EditCondition = "Tool == EMazeEditTool::Cells", EditConditionHides))
	EMazeCellType PaintType = EMazeCellType::Solid;

	/** Which object type is being placed. Set by clicking a swatch in the object palette. */
	UPROPERTY(VisibleAnywhere, config, Category = "Brush",
		meta = (EditCondition = "Tool == EMazeEditTool::Objects", EditConditionHides))
	FName PaintObjectType;

	/**
	 *  Which depth band new objects go into.
	 *
	 *  A band and not a slice, for the same reason the placement stores one: depth profiles get
	 *  edited, and a remembered cell index would quietly start meaning somewhere else.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Brush",
		meta = (EditCondition = "Tool == EMazeEditTool::Objects", EditConditionHides))
	EMazeDepthBand PaintBand = EMazeDepthBand::Play;

	UPROPERTY(EditAnywhere, config, Category = "Brush", meta = (ClampMin = "1", ClampMax = "16",
		EditCondition = "Tool == EMazeEditTool::Cells", EditConditionHides))
	int32 BrushSize = 1;

	/**
	 *  Surface variant written into the painted cell.
	 *
	 *  Normally picked by clicking a swatch at the top of the mode panel — that row is built from
	 *  the palette of the current paint type and shows the actual colours. The number is kept
	 *  visible for the cases a swatch cannot serve: reading off what is selected, or typing an
	 *  index straight in.
	 *
	 *  Which variants exist is decided by the palette in Build Settings, per cell type. An index
	 *  the type does not declare is not an error — the cell simply gets the base surface.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Brush", meta = (ClampMin = "0", ClampMax = "63"))
	int32 PaintVariant = 0;

	UPROPERTY(EditAnywhere, config, Category = "Brush")
	EMazeDepthApply DepthApply = EMazeDepthApply::FillBands;

	/** Active depth slice: the plane the cursor is caught on. PgUp / PgDn. */
	UPROPERTY(EditAnywhere, Category = "Brush", meta = (ClampMin = "0"))
	int32 ActiveDepthSlice = 0;

	/**
	 *  Style preset for the overlay: colours and thicknesses of the grid overlay, rooms and brush.
	 *
	 *  Empty means the built-in defaults, that is exactly the previous look. Your own preset is
	 *  created through the Content Browser: Miscellaneous → Data Asset →
	 *  Maze Editor Style Asset.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Display")
	TSoftObjectPtr<UMazeEditorStyleAsset> Style;

	UPROPERTY(EditAnywhere, config, Category = "Display")
	bool bShowGrid = true;

	UPROPERTY(EditAnywhere, config, Category = "Display")
	bool bShowRooms = true;

	UPROPERTY(EditAnywhere, config, Category = "Display")
	bool bShowPreview = true;

	/**
	 *  Bring the layer being painted to the front of the preview and dim the rest.
	 *
	 *  Painting a back wall inside a niche is otherwise blind work: the maze mass stands between
	 *  the camera and the wall, and the brush lands on cells nobody can see. In the Left and Right
	 *  orthographic views this costs nothing in accuracy — those views have no perspective along Y,
	 *  so moving a layer towards the camera changes what covers what and not where anything looks
	 *  like it is. In a perspective viewport the shifted layer does slide a little; turn this off
	 *  if that gets in the way.
	 */
	/**
	 *  Draw the room grid the slicer WOULD produce, instead of the slicing that was stored.
	 *
	 *  Nothing is cut and nothing is written: the lattice is computed from the slicer's settings
	 *  and thrown away after it is drawn. It answers "where will the seams fall if I slice with
	 *  these numbers" before the slicing exists — and after it exists, it is how you see the
	 *  effect of changing Room Size without re-slicing and invalidating every baked mesh.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Display")
	bool bShowRoomPreview = false;

	/**
	 *  Take the previous maze off the map when the target changes.
	 *
	 *  On by default, and normally right: another maze's real geometry standing behind this
	 *  maze's preview cubes is the single most confusing state the mode can be in.
	 *
	 *  Turn it off to test a door between two mazes in PIE. The pool builds its streaming
	 *  entries itself in a packaged game, but in PIE it can only use levels already attached to
	 *  the persistent map — so both mazes have to be attached at once, and the automatic detach
	 *  undoes that the moment you switch the target to attach the second one.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Display")
	bool bDetachPreviousOnTargetSwitch = true;

	UPROPERTY(EditAnywhere, config, Category = "Display")
	bool bIsolateActiveLayer = true;

	/**
	 *  Hold this key to suspend the isolation for as long as it is held.
	 *
	 *  Isolation is what lets a back wall be painted at all, and it is also what hides everything
	 *  the wall now stands in front of: paint over a large stretch and the floors that were there
	 *  are gone from view. Peeking answers "what was underneath" without touching the brush — the
	 *  layers drop back into their true depth order, and releasing the key returns to painting.
	 *
	 *  A key rather than an opacity slider, and that is a deliberate limit rather than a shortcut.
	 *  The preview is drawn with the engine's opaque BasicShapeMaterial, and blend mode is a static
	 *  property of a material: a dynamic instance cannot turn it translucent, so real see-through
	 *  would mean shipping a material with the plugin. It would also look poor — after Build Depth
	 *  Volume a column holds dozens of identical cubes, and translucent surfaces that overlap that
	 *  heavily sort badly against each other.
	 *
	 *  Alt is not available: it is the viewport orbit, and LMB is deliberately ignored while it is
	 *  down.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Display")
	FKey PeekKey = EKeys::Q;

	/**
	 *  How strongly the layers that are NOT being painted are held back. 1 — not at all.
	 *
	 *  This is the transparency slider, in the only form the preview can honestly offer one: the
	 *  cubes are drawn with an opaque material, so "see-through" is not available, but "clearly
	 *  present and clearly not the layer you are working on" is.
	 *
	 *  It is a blend toward a dark neutral rather than a multiply. Multiplying is what the first
	 *  version did, and it worked only for bright colours: the back wall ships in the 0.2-0.3
	 *  range, and 30% of that on a near-black viewport background is nothing at all — the layer
	 *  did not read as dimmed, it read as missing.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Display",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float InactiveLayerDim = 0.45f;

	/**
	 *  Rebuild every room's mesh instead of only the ones that changed.
	 *
	 *  The bake normally compares each room against a hash of what it is made of and leaves the
	 *  unchanged ones alone. That comparison cannot see the meshes on disk being deleted or
	 *  edited by hand, so this is the way out when the two have drifted apart.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Advanced")
	bool bForceFullRebake = false;

	/**
	 *  Fallback size of the grid overlay window in cells. Normally the grid overlay is drawn over
	 *  the visible area of the viewport; this value is only used where the frame does not cross
	 *  the slice plane and the visible area cannot be computed.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Display", meta = (ClampMin = "8", ClampMax = "256"))
	int32 GridWindowCells = 48;

	// --------------------------------------------------------------------- actions
	//
	//  The whole maze pipeline lives here rather than in the asset's Details: the parameters are
	//  set up once, but the work happens in the mode, and there is no reason to switch between
	//  two panels at every step.
	//
	//  The panel reads top to bottom as the job actually goes: pick a target, generate or draw,
	//  look at it, save a state you like, build it. The order is NOT the alphabetical order the
	//  engine would give these categories — FMazeEdModeSettingsDetails sets it explicitly with
	//  SetSortOrder, which is why the names here carry no leading digits any more.
	//
	//  Three of the actions are missing CallInEditor on purpose. A generated button cannot be
	//  disabled conditionally, and these three must be: generating into a manual grid, and the
	//  edit cycle before there is anything to edit. They are built by hand in the customization.

	/** Runs the target's generator. Meaningless while the generator is the manual one. */
	void GenerateMaze();

	/** Detaches the room levels and flattens the grid, ready for the brush. */
	void ChangeCurrentMaze();

	/** Volume, rooms, meshes, levels — and the levels back into the map. */
	void ApplyChangesToCurrentMaze();

	/** True while the target's generator produces nothing on its own. */
	bool IsGeneratorManual() const;

	/** The generator's own display name, for the caption under a disabled Generate. */
	FText GetGeneratorName() const;

	/** A maze that has been sliced into rooms exists. The edit cycle needs one. */
	bool HasBuiltMaze() const;

	UFUNCTION(CallInEditor, Category = "Brush",
		meta = (DisplayName = "Fill Back Wall", DisplayPriority = "1"))
	void FillBackWall();

	UFUNCTION(CallInEditor, Category = "Brush",
		meta = (DisplayName = "Clear Back Wall", DisplayPriority = "2"))
	void ClearBackWall();

	/**
	 *  Removes every placement from the target's spawn asset.
	 *
	 *  Its own button and NOT part of Clear All Changes, which erases the drawing. The two are
	 *  different work at different cadences: the maze is redrawn many times, the decor pass is
	 *  made once and slowly. And the snapshot does not hold placements, so folding them into
	 *  Clear All Changes would make that loss the only irreversible one in the panel.
	 */
	UFUNCTION(CallInEditor, Category = "Objects",
		meta = (DisplayName = "Clear All Objects", DisplayPriority = "1"))
	void ClearAllObjects();

	/** The target's spawn asset, or null. */
	UMazeSpawnAsset* GetSpawnAsset() const;

	/** The library the target's spawn asset points at, or null. */
	UMazeObjectLibrary* GetObjectLibrary() const;

	UFUNCTION(CallInEditor, Category = "Snapshot",
		meta = (DisplayName = "Save", DisplayPriority = "1"))
	void SaveSnapshot();

	UFUNCTION(CallInEditor, Category = "Snapshot",
		meta = (DisplayName = "Restore", DisplayPriority = "2"))
	void RestoreSnapshot();

	/**
	 *  The whole build in one press: volume, rooms, meshes, levels, levels into the map.
	 *
	 *  Same steps as Apply Changes To Current Maze, and deliberately a separate button. This one
	 *  is the first build of a maze that has only ever been drawn; that one closes an edit cycle
	 *  that Change Current Maze opened. Sharing a button would mean the label lies about the
	 *  state you are in half the time.
	 */
	UFUNCTION(CallInEditor, Category = "Build",
		meta = (DisplayName = "Apply Changes", DisplayPriority = "1"))
	void ApplyChanges();

	/** Erases the drawing. A snapshot is taken first, so Restore brings it back. */
	UFUNCTION(CallInEditor, Category = "Build",
		meta = (DisplayName = "Clear All Changes", DisplayPriority = "2"))
	void ClearMaze();

	/**
	 *  Opens the world map — how this game's mazes join up.
	 *
	 *  Here rather than only under Window because it belongs to this job: the transition points
	 *  are drawn with the brush two groups above, and the links between them are drawn there.
	 */
	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Open World Map", DisplayPriority = "0"))
	void OpenWorldMap();

	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Build Depth Volume", DisplayPriority = "1"))
	void BuildDepthVolume();

	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Flatten To Plane", DisplayPriority = "2"))
	void FlattenToPlane();

	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Slice Into Rooms", DisplayPriority = "3"))
	void SliceIntoRooms();

	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Build Room Meshes", DisplayPriority = "4"))
	void BakeRoomMeshes();

	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Export Rooms To Levels", DisplayPriority = "5"))
	void ExportRoomsToLevels();

	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Attach Rooms To Level", DisplayPriority = "6"))
	void AttachRoomsToLevel();

	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Detach Rooms From Level", DisplayPriority = "7"))
	void DetachRoomsFromLevel();

	DECLARE_MULTICAST_DELEGATE(FOnMazeSettingsChanged);
	FOnMazeSettingsChanged OnSettingsChanged;

	/** Rebuilds the Status line from the current state of the target. */
	void RefreshStatus();

	/** The style preset, or the built-in default. Never returns null. */
	const UMazeEditorStyleAsset& GetStyle() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
