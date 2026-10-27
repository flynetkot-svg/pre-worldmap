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
class UMazeSpawnRulesAsset;
class UMazeWorldGraph;

/** What the mouse does in the viewport. */
UENUM()
enum class EMazeEditTool : uint8
{
	/** Paint the mass of the maze: Solid, Floor, BackWall. */
	Cells   = 0,
	/** Place objects from the library. */
	Objects = 1,
	/**
	 *  Fuse lattice cells into single rooms.
	 *
	 *  Not a brush over the maze but an edit of how it is cut up, which is why it gets a tool
	 *  of its own rather than a modifier: a stroke here changes no cell and draws nothing, and
	 *  sharing a button with the paint brush would make "what did that click just do" depend
	 *  on a checkbox somewhere else.
	 */
	Rooms   = 2,
	/**
	 *  Plan the maze's paths for the Classic generator (Style Path First): click — a point on
	 *  the main path, Shift+click — a point on the last branch, Ctrl+Shift+click — a new branch,
	 *  Ctrl+click — remove the nearest point.
	 */
	Path    = 3,
	/**
	 *  Stand a ladder: first click — its bottom (snaps down to the floor, or to an exit level of
	 *  the shaft, so ladders stack), second click — its top (snaps down to the nearest exit).
	 *  One ladder the whole length, a platform on top. Shift+click — cancel the first click, or
	 *  remove the ladders in that column. Which ladder: a Climb swatch in the palette.
	 */
	Ladder  = 4
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

/** A stretch of the path overlay, in cells. Kind 0 — main path; 1, 2, … — branch level. */
struct FMazePathOverlayLine
{
	FVector2f A = FVector2f::ZeroVector;
	FVector2f B = FVector2f::ZeroVector;
	uint8 Kind = 0;
};

/** A marker of the path overlay, in cells. Kind: 1 dead end, 2 junction, 3 start, 4 exit. */
struct FMazePathOverlayPoint
{
	FVector2f At = FVector2f::ZeroVector;
	uint8 Kind = 0;
};

/** One shaded rectangle of the last reachability check: 1 unreachable, 2 reached in part, 3 behind a door. */
struct FMazeReachOverlayRect
{
	FIntRect Rect;
	uint8 Status = 0;
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
	 *  Generate the maze again as soon as a setting under Maze Features is committed.
	 *
	 *  Committed, not dragged: a slider regenerates when it is let go, not on every step. Off —
	 *  only Generate Maze does. Every run snapshots the drawing first, as the button does.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Maze Features")
	bool bRegenerateOnChange = true;

	// What Find Seed aims for, read off Analyze Paths. 0 — not a target.

	/** Dead ends the maze should have. */
	UPROPERTY(EditAnywhere, config, Category = "Maze Features", meta = (ClampMin = "0", UIMax = "200"))
	int32 TargetDeadEnds = 0;

	/** Average branch depth, in grid cells, the maze should at least reach. */
	UPROPERTY(EditAnywhere, config, Category = "Maze Features", meta = (ClampMin = "0", UIMax = "1000"))
	int32 TargetMinBranchDepth = 0;

	/** Levels of branch-off-a-branch the maze should have. */
	UPROPERTY(EditAnywhere, config, Category = "Maze Features", meta = (ClampMin = "0", UIMax = "8"))
	int32 TargetNesting = 0;

	/** Confusion score (0–100) the maze should at least reach. */
	UPROPERTY(EditAnywhere, config, Category = "Maze Features", meta = (ClampMin = "0", ClampMax = "100"))
	int32 TargetMinConfusion = 50;

	/** How many seeds Find Seed tries. */
	UPROPERTY(EditAnywhere, config, Category = "Maze Features", meta = (ClampMin = "1", UIMax = "200"))
	int32 SeedTries = 20;

	/**
	 *  Generates the maze with Seed Tries seeds, analyses each, and keeps the one closest to the
	 *  targets above. Any generator. One undo step back to before the search.
	 */
	UFUNCTION(CallInEditor, Category = "Maze Features", meta = (DisplayName = "Find Seed"))
	void FindSeed();

	/** Removes the main path and branches planned with the Path tool. The next generation is random again. */
	UFUNCTION(CallInEditor, Category = "Maze Features", meta = (DisplayName = "Clear Path Plan"))
	void ClearPathPlan();

	// ------------------------------------------------------------------ reachability
	//
	// Can the player get everywhere from where he arrives? Without Require Ladders: any space
	// he fits in, in any direction — ladders and lifts are assumed to come later, and what is
	// found is the pieces nothing could lead into. With it: walking, falling, stepping or jumping
	// up, and climbing only Climb-category objects. Starts at the Entry transition points; with
	// none, at the lowest place to stand.

	/**
	 *  Count climbing only where a ladder (any Climb object) stands. Off — every shaft he fits
	 *  in counts as a way up, on the understanding that a ladder or a lift will stand there.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Reachability")
	bool bRequireLadders = false;

	/** How wide the player is, in cm: the stock capsule is 84. A slit narrower than this is a wall. */
	UPROPERTY(EditAnywhere, config, Category = "Reachability", meta = (ClampMin = "1", UIMax = "300"))
	int32 AgentWidthCm = 84;

	/** How high he steps up without jumping, in cm. The stock character manages 45. Require Ladders only. */
	UPROPERTY(EditAnywhere, config, Category = "Reachability", meta = (ClampMin = "0", UIMax = "200",
		EditCondition = "bRequireLadders"))
	int32 StepUpCm = 45;

	/** How high a jump gets him, in cm. 0 — the check assumes he does not jump. Require Ladders only. */
	UPROPERTY(EditAnywhere, config, Category = "Reachability", meta = (ClampMin = "0", UIMax = "500",
		EditCondition = "bRequireLadders"))
	int32 JumpHeightCm = 0;

	/**
	 *  The ladder Fix Reachability stands in a shaft that has to be climbed. Empty — the Classic
	 *  generator's Ladder Type, if the target has one. With neither, places above the reachable
	 *  part are reported, not fixed. Require Ladders only: without it no ladder is placed.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Reachability", meta = (EditCondition = "bRequireLadders"))
	FName FixLadderType;

	/** Width of the shafts Fix Reachability cuts, in cells. */
	UPROPERTY(EditAnywhere, config, Category = "Reachability", meta = (ClampMin = "2", UIMax = "16"))
	int32 FixShaftWidthCells = 2;

	/** What the last check found. Rooms it could not reach are hatched red, rooms reached in part orange. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Reachability")
	FString ReachabilityStatus;

	UFUNCTION(CallInEditor, Category = "Reachability",
		meta = (DisplayName = "Check Reachability", DisplayPriority = "1"))
	void CheckReachability();

	/**
	 *  Joins every unreachable piece to the rest: a tunnel along the floor and a shaft, with a
	 *  ladder where the way leads up. One undo step.
	 */
	UFUNCTION(CallInEditor, Category = "Reachability",
		meta = (DisplayName = "Fix Reachability", DisplayPriority = "2"))
	void FixReachability();

	/**
	 *  The maze as the player walks it: the main path from the start to the exit (a Gate point,
	 *  or the farthest place), the branches off it, the branches off those, dead ends, loops and
	 *  a confusion score. Drawn over the maze: main path yellow, branches by level, dead ends red.
	 */
	UFUNCTION(CallInEditor, Category = "Reachability",
		meta = (DisplayName = "Analyze Paths", DisplayPriority = "3"))
	void AnalyzePaths();

	/** What the last Analyze Paths found. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Reachability")
	FString PathStatus;

	TArray<FMazePathOverlayLine> PathLines;
	TArray<FMazePathOverlayPoint> PathPoints;

	// ---------------------------------------------------------------------- locks
	//
	// Doors and keys: library types with Lock Role Door or Key, paired by the Lock Id on the
	// placement. Check Reachability plays them out — keys behind other doors included — and
	// reports keys that cannot be had, doors that never open, and keys too close to their door.

	/** How many doors Generate Locks places, each with its key. */
	UPROPERTY(EditAnywhere, config, Category = "Locks", meta = (ClampMin = "0", UIMax = "16"))
	int32 LockCount = 2;

	/** The library type for doors. Empty — the first type with Lock Role Door. */
	UPROPERTY(EditAnywhere, config, Category = "Locks")
	FName LockDoorType;

	/** The library type for keys. Empty — the first type with Lock Role Key. */
	UPROPERTY(EditAnywhere, config, Category = "Locks")
	FName LockKeyType;

	/** Lock Ids handed out in turn. More locks than names — a colour opens more than one door. */
	UPROPERTY(EditAnywhere, config, Category = "Locks")
	TArray<FName> LockIds = { FName(TEXT("Red")), FName(TEXT("Blue")), FName(TEXT("Yellow")) };

	/**
	 *  Rooms that must lie between a door and its key. 1 — never in the same room or in rooms
	 *  next to each other, so the two are never loaded together around the player.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Locks", meta = (ClampMin = "0", UIMax = "8"))
	int32 MinRoomsBetweenKeyAndDoor = 1;

	/** Most rooms between a door and its key. 0 — no limit. */
	UPROPERTY(EditAnywhere, config, Category = "Locks", meta = (ClampMin = "0", UIMax = "64"))
	int32 MaxRoomsBetweenKeyAndDoor = 0;

	/** Most doors in a row a key may hide behind: 1 — keys lie in the open; 2 — behind one door. */
	UPROPERTY(EditAnywhere, config, Category = "Locks", meta = (ClampMin = "1", UIMax = "8"))
	int32 MaxLockNesting = 3;

	/** Most keys that come into reach at once, before any of their doors can be opened. */
	UPROPERTY(EditAnywhere, config, Category = "Locks", meta = (ClampMin = "1", UIMax = "8"))
	int32 MaxKeysHeld = 3;

	/** The most of the maze, as a share of the places to stand, one door may close off. */
	UPROPERTY(EditAnywhere, config, Category = "Locks", meta = (ClampMin = "0.05", ClampMax = "1"))
	float MaxLockedShare = 0.6f;

	/** A door is only worth placing if it closes off at least this many places to stand. */
	UPROPERTY(EditAnywhere, config, Category = "Locks", meta = (ClampMin = "1", UIMax = "10000"))
	int32 MinLockedCells = 20;

	/** Same seed, same maze — same doors and keys. */
	UPROPERTY(EditAnywhere, config, Category = "Locks")
	int32 LockSeed = 1;

	/**
	 *  Places Lock Count doors where each closes off a piece of the maze, and a key for each
	 *  where it can be had without that door, Min Rooms Between away. Doors and keys placed by
	 *  hand stay and are taken into account; the ones from the last run are replaced.
	 */
	UFUNCTION(CallInEditor, Category = "Locks", meta = (DisplayName = "Generate Locks", DisplayPriority = "1"))
	void GenerateLocks();

	UFUNCTION(CallInEditor, Category = "Locks", meta = (DisplayName = "Clear Generated Locks", DisplayPriority = "2"))
	void ClearGeneratedLocks();

	/** The shaded rooms of the last check. */
	TArray<FMazeReachOverlayRect> ReachRoomOverlay;

	/** Doors and keys the last check found something wrong with: framed in red. */
	TArray<FIntRect> ReachProblemRects;

	/** Bounds of the unreached pieces, drawn when the maze is not sliced into rooms yet. */
	TArray<FIntRect> ReachIslandBounds;

	/** The asset the overlay belongs to. Any change to its grid drops the overlay. */
	TWeakObjectPtr<const UMazeGridAsset> ReachAsset;

	void ClearReachability();

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

	/**
	 *  Which object type is being placed. Set by clicking a swatch in the object palette: a
	 *  Climb type picks the Ladder tool, anything else the object brush.
	 */
	UPROPERTY(VisibleAnywhere, config, Category = "Brush",
		meta = (EditCondition = "Tool == EMazeEditTool::Objects || Tool == EMazeEditTool::Ladder",
			EditConditionHides))
	FName PaintObjectType;

	/**
	 *  The Lock Id given to doors and keys placed with the brush: one red key opens every red
	 *  door. Ignored for objects that are neither.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Brush",
		meta = (EditCondition = "Tool == EMazeEditTool::Objects", EditConditionHides))
	FName PaintLockId = TEXT("Red");

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
	 *
	 *  Deliberately not remembered between editor sessions. As a config setting it outlived the
	 *  one rebuild it was ticked for and quietly turned every later Apply into a full rebake.
	 */
	UPROPERTY(EditAnywhere, Category = "Advanced")
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

	/** Moves hand-placed decor into the levels of the rooms it stands in. See FMazeDecorBinder. */
	void BindDecorToRooms();

	/** True while the target's generator produces nothing on its own. */
	bool IsGeneratorManual() const;

	/** The generator's own display name, for the caption under a disabled Generate. */
	FText GetGeneratorName() const;

	/**
	 *  The seed of the target's generator, and a way to change it.
	 *
	 *  Surfaced in the mode panel rather than left in the asset because of how it is actually
	 *  used: a seed is not configured once, it is rolled. Deciding that a maze is the wrong
	 *  one is a judgement made while looking at the viewport, and having to open a second
	 *  asset to act on it turns half a second into a detour — which mostly ends with the
	 *  designer keeping a layout they did not like.
	 *
	 *  Zero when there is no target or no generator, and setting it is then ignored.
	 */
	int32 GetGeneratorSeed() const;
	void SetGeneratorSeed(int32 NewSeed);

	/** A new seed and a fresh maze in one press. */
	void RerollGeneratorSeed();

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

	/**
	 *  How much of what a generated pass scatters. Read by Generate Objects only.
	 *
	 *  Its own asset, shared by every maze: what a crate is belongs to the library, which maze
	 *  it is in belongs to the grid, and how thickly crates are strewn is a decision about the
	 *  game that ten mazes should be able to share and change in one place.
	 */
	UPROPERTY(EditAnywhere, Category = "Objects", meta = (DisplayPriority = "2"))
	TSoftObjectPtr<UMazeSpawnRulesAsset> SpawnRules;

	/**
	 *  Furnishes every room of the target from the rules above.
	 *
	 *  Replaces what a previous run of this button placed and touches nothing else. Objects put
	 *  down by hand — transition points first among them — are kept exactly where they are and
	 *  treated as ground that is already taken, so running this twice is as safe as running it
	 *  once, and a gate can never be scattered away.
	 */
	UFUNCTION(CallInEditor, Category = "Objects",
		meta = (DisplayName = "Generate Objects", DisplayPriority = "3"))
	void GenerateObjects();

	/** Removes what Generate Objects placed, leaving everything put down by hand. */
	UFUNCTION(CallInEditor, Category = "Objects",
		meta = (DisplayName = "Clear Generated Objects", DisplayPriority = "4"))
	void ClearGeneratedObjects();

	/**
	 *  Stands a ladder in every hole of the maze that has none yet. The type: a Climb swatch
	 *  picked in the palette, else Fix Ladder Type, else the Classic generator's Ladder Type.
	 *  Placed as part of the maze — the next generation replaces them. One undo step.
	 */
	UFUNCTION(CallInEditor, Category = "Brush",
		meta = (DisplayName = "Place All Ladders", DisplayPriority = "3"))
	void PlaceAllLadders();

	/** Removes every ladder and ladder platform from the maze — placed by hand or not. One undo step. */
	UFUNCTION(CallInEditor, Category = "Brush",
		meta = (DisplayName = "Clear All Ladders", DisplayPriority = "4"))
	void ClearAllLadders();

	/**
	 *  Saves every ladder and platform now placed into the maze's ladder map (in its Spawns
	 *  asset), replacing the previous one. Save the asset afterwards to keep it on disk.
	 */
	UFUNCTION(CallInEditor, Category = "Brush",
		meta = (DisplayName = "Save Ladder Map", DisplayPriority = "5"))
	void SaveLadderMap();

	/** Replaces the ladders now placed with the saved ladder map. One undo step. */
	UFUNCTION(CallInEditor, Category = "Brush",
		meta = (DisplayName = "Load Ladder Map", DisplayPriority = "6"))
	void LoadLadderMap();

	/**
	 *  The ladder type the Ladder tool and Place All Ladders use: the Climb type picked in the
	 *  palette, else Fix Ladder Type, else the Classic generator's Ladder Type. None if none.
	 */
	FName ResolveLadderType() const;

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
	 *  Moves room levels and meshes this maze's manifest no longer uses into Deprecated.
	 *
	 *  Apply only takes abolished rooms off the map; it leaves their assets where they are,
	 *  because moving them costs about a second per asset inside the engine's rename — nine
	 *  minutes on the first big map, which is not a price to pay for seeing a change. This is
	 *  where that price is paid, once, when the folder is worth tidying. It asks first, with
	 *  the count.
	 */
	UFUNCTION(CallInEditor, Category = "Build",
		meta = (DisplayName = "Move Stale Rooms To Deprecated",
			ToolTip = "Move room levels and meshes the last build no longer uses into a Deprecated folder. Nothing is deleted."))
	void MoveStaleRoomsToDeprecated();

	// --------------------------------------------------------------------------- the world
	//
	// Everything here is about the game as a whole rather than the maze on the screen: which
	// mazes exist, how they join up, and whether the open map knows about all of them. It was
	// scattered between Advanced and nowhere, which put the one button a designer needs after
	// cloning the repository at the bottom of a section folded away by default.

	/**
	 *  The world graph, read by Attach All Mazes and by nothing else here.
	 *
	 *  The same asset the character's streaming component points at. It is named here as well
	 *  because attaching is editor work, done with the map open and the character closed.
	 */
	UPROPERTY(EditAnywhere, Category = "World", meta = (DisplayPriority = "0"))
	TSoftObjectPtr<UMazeWorldGraph> WorldGraph;

	/**
	 *  Opens the world map — how this game's mazes join up.
	 *
	 *  Here rather than only under Window because it belongs to this job: the transition points
	 *  are drawn with the brush a few groups above, and the links between them are drawn there.
	 */
	UFUNCTION(CallInEditor, Category = "World",
		meta = (DisplayName = "Open World Map", DisplayPriority = "1"))
	void OpenWorldMap();

	/**
	 *  Puts every maze the world graph names onto the open map, in one action.
	 *
	 *  Nothing is generated, exported or re-baked: this only makes the map agree with the
	 *  graph. That is the state a fresh clone leaves it in, and a hand detach, and a maze that
	 *  was built but never attached. The alternative is Apply Changes once per maze —
	 *  rebuilding every mesh of every maze in order to add lines to a list of levels.
	 */
	UFUNCTION(CallInEditor, Category = "World",
		meta = (DisplayName = "Attach All Mazes In World Graph", DisplayPriority = "2"))
	void AttachAllMazes();

	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Build Depth Volume", DisplayPriority = "1"))
	void BuildDepthVolume();

	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Flatten To Plane", DisplayPriority = "2"))
	void FlattenToPlane();

	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Slice Into Rooms", DisplayPriority = "3"))
	void SliceIntoRooms();

	/**
	 *  Throws away every merge, leaving the plain lattice. The slicing itself is untouched
	 *  until the next Apply Changes.
	 *
	 *  Its own button because the alternative is Shift-dragging over each merge in turn, and
	 *  the case that needs it most is the one where the designer has cut a fine lattice into
	 *  a patchwork and wants to start over — precisely when there are too many to undo by
	 *  hand.
	 */
	UFUNCTION(CallInEditor, Category = "Advanced",
		meta = (DisplayName = "Clear All Room Merges",
			ToolTip = "Undo every merge and go back to the plain lattice."))
	void ClearRoomMerges();

	/**
	 *  Hides or shows this maze's room levels in the editor.
	 *
	 *  The room frames and the merge hatching are lines drawn over whatever is in the
	 *  viewport, and once the levels are attached that is a solid built maze. Hiding them
	 *  leaves the drawing and the slicing, which is what this tool is actually editing.
	 */
	UFUNCTION(CallInEditor, Category = "Display",
		meta = (DisplayName = "Show / Hide Room Levels",
			ToolTip = "Hide the built room levels so only the drawing and the slicing show."))
	void ToggleRoomLevels();

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
