#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/MazeGrid.h"
#include "Data/MazeRoomDesc.h"
#include "MazeGridAsset.generated.h"

class UMazeGeneratorBase;
class UMazeRoomSlicerBase;
class UMazeBuildSettings;
class UMazeSpawnAsset;
class UMazeWorldManifest;

/**
 *  The maze asset — the source of truth for the whole pipeline.
 *
 *  It holds the grid, the generator instance, the slicer instance and the slicing result.
 *
 *  There are no buttons in Details: this is a parameter asset, it is configured once.
 *  The pipeline actions — generation, volume, slicing, mesh bake, export — are gathered
 *  in the edit mode panel and call the methods of this class directly.
 */
UCLASS(BlueprintType)
class MAZEFORGECORE_API UMazeGridAsset : public UObject
{
	GENERATED_BODY()

public:
	UMazeGridAsset();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Maze")
	FMazeGrid Grid;

	/** What fills the grid. Changing the class changes the mode: manual / random / from an image. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Generation")
	TObjectPtr<UMazeGeneratorBase> Generator;

	/** What cuts it into streaming rooms. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Slicing")
	TObjectPtr<UMazeRoomSlicerBase> Slicer;

	/** The slicing result. Filled in by SliceIntoRooms. */
	UPROPERTY(VisibleAnywhere, Category = "Slicing")
	TArray<FMazeRoomDesc> Rooms;

	/**
	 *  Short name of this maze. Separates its output from every other maze's.
	 *
	 *  Empty is the old behaviour exactly, byte for byte, so existing mazes keep their assets.
	 *  Set it and everything this maze produces moves into a compartment of its own: the levels,
	 *  meshes and manifest go into a subfolder named after it, and the asset names carry it.
	 *
	 *  It exists because room ids are not unique across mazes — every uniform slicing starts at
	 *  R_000_000 — so two mazes sharing one set of build settings write the same level names,
	 *  the same mesh names and the same manifest. Building the test maze silently overwrote the
	 *  production one, and nothing said so.
	 *
	 *  Giving each maze its own copy of the build settings would work too, and would duplicate
	 *  the palette, the surface variants and the bake flags along with the paths. Those answer
	 *  "how to build" and are worth sharing; only "where it lands" belongs to the maze.
	 *
	 *  Anything that is not a letter, a digit or an underscore is stripped: this becomes a
	 *  package path segment.
	 */
	UPROPERTY(EditAnywhere, Category = "Build")
	FString MazeName;

	/** MazeName with everything a package path cannot carry removed. Empty stays empty. */
	FString GetSafeMazeName() const;

	/** Palette and bake rules for the export. */
	UPROPERTY(EditAnywhere, Category = "Build")
	TSoftObjectPtr<UMazeBuildSettings> BuildSettings;

	/**
	 *  Where this maze's objects stand. Optional — a maze with no decor needs none.
	 *
	 *  A separate asset because it outlives the grid: the drawing is generated, cleared and
	 *  redrawn many times, while the decor pass is a slower and more expensive investment made
	 *  later. Clear All Changes erases the drawing and leaves this alone.
	 */
	UPROPERTY(EditAnywhere, Category = "Build")
	TSoftObjectPtr<UMazeSpawnAsset> Spawns;

	/**
	 *  The world manifest written by the last export. An output, not a setting.
	 *
	 *  The game does NOT read it from here. The runtime reads the Manifest field on the
	 *  MazeForge Streaming component of whatever is watching the player — normally the character
	 *  blueprint — and that field is set by hand, once, and never follows this one.
	 *
	 *  The two together cost an evening. This row shows the freshly built manifest of the current
	 *  Maze Name, is greyed out like something the system is managing for you, and reads as the
	 *  authoritative answer — while the game went on loading a manifest that had since been
	 *  deleted. The old comment here even said "this is where the runtime takes it from", which
	 *  is exactly the thing that is not true. Giving a maze a Maze Name gives it its own manifest,
	 *  and copying that onto the character is still a manual step.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Build",
		meta = (DisplayName = "Built Manifest (output)"))
	TSoftObjectPtr<UMazeWorldManifest> Manifest;

	/**
	 *  Revision number of the grid. Increases on any change to it.
	 *
	 *  A coarse "has anything changed at all" marker, used to drop cached per-room answers. What
	 *  actually decides whether a room needs rebuilding is its own hash, below — a single number
	 *  for the whole asset can only ever say that something, somewhere, moved.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Build")
	int32 GridRevision = 0;

	/**
	 *  What each room looked like when its meshes were last baked, one number per room.
	 *
	 *  GridRevision alone can only answer "has anything changed anywhere", and after cutting one
	 *  hole in one room the honest answer to that is yes — which used to mean rebuilding all 208
	 *  rooms to rebuild one. This says which rooms actually changed.
	 *
	 *  int64 and not uint64 because UHT does not accept unsigned 64-bit properties. The sign is
	 *  meaningless here; the number is only ever compared for equality.
	 */
	UPROPERTY()
	TMap<FName, int64> BakedRoomHashes;

	/**
	 *  Everything that decides what this room's mesh will look like, boiled down to one number.
	 *
	 *  Includes the room's own cells, the cells one step OUTSIDE its bounds (face culling asks
	 *  the neighbours, so a wall built in the room next door changes the faces along the seam),
	 *  the geometry of the grid, the depth profile, the bake flags and the palette.
	 */
	int64 ComputeRoomHash(const FMazeRoomDesc& Room) const;

	/** Whether this room's baked meshes still match what the grid says it should be. */
	bool IsRoomBaked(const FMazeRoomDesc& Room) const;

	/**
	 *  How many rooms are baked and unchanged. Memoised.
	 *
	 *  The memo is not an optimisation for its own sake. Hashing one room walks its cells plus a
	 *  skirt, and the status line asks this after every button and every field edited in the
	 *  panel — on 208 rooms that is a few million lookups for a string nobody is reading yet.
	 */
	int32 CountBakedRooms() const;

	/** Forget the memo. Called whenever the grid or the baked hashes change. */
	void InvalidateBakedRoomCache() const { CachedBakedRooms = INDEX_NONE; }

	/** Whether every room is baked and up to date. Used by the status line and the export. */
	bool AreBakedMeshesCurrent() const;

	/** Drops hash entries for rooms that no longer exist. Called after a re-slice. */
	void PruneBakedRoomHashes();

private:
	/** INDEX_NONE — not counted yet. Transient by design: it is derived from what is saved. */
	mutable int32 CachedBakedRooms = INDEX_NONE;

	/** The build settings revision the count above was taken at. See UMazeBuildSettings. */
	mutable int32 CachedSettingsRevision = INDEX_NONE;

public:

	/** The editor and the preview listen to it: the grid changed, rebuild the display. */
	DECLARE_MULTICAST_DELEGATE(FOnMazeGridChanged);
	FOnMazeGridChanged OnGridChanged;

	void NotifyGridChanged();

	/** Creates the starting generator and slicer if they are not set. Called by the factory. */
	void EnsureDefaults();

	virtual void PostLoad() override;

	// --------------------------------------------------------------------- actions
	//
	//  There are deliberately no buttons here. The asset is the parameters: depth, bounds,
	//  generator, slicer; they are configured once. The actions live in the edit mode panel
	//  (UMazeEdModeSettings), because that is where the work happens, not in the asset's
	//  Details. All the methods below are public — the panel calls them directly.

	void RunGenerator();

	void SliceIntoRooms();

	void ClearGrid();

	/**
	 *  Grows the volume along the depth from the flat layout.
	 *
	 *  The generators and the brush work in a single plane: on a 707x376 map that is tens
	 *  of thousands of cells instead of hundreds of thousands, and the preview stays
	 *  responsive. The button stretches the finished layout over the Background / Play /
	 *  Foreground profile.
	 *
	 *  The operation is reversible: Flatten To Plane gives back exactly the same plane.
	 *  Ladder and Spawn markers stay a single cell — they are data, not geometry.
	 */
	void BuildDepthVolume();

	/**
	 *  Flattens the grid back into a plane for editing with the brush.
	 *
	 *  Nothing is lost: a column that had at least one solid cell stays a cell in the
	 *  plane. After the edit, Build Depth Volume is pressed again.
	 */
	void FlattenToPlane();

	/**
	 *  Paints the back wall behind every empty column of the play plane.
	 *
	 *  A starting point, not the final look. Back wall is off by default and painting a
	 *  707x376 map by hand is hours of work, while a forgotten patch is a hole into the skybox
	 *  that only shows up in game. One click covers everything the camera can actually see —
	 *  behind solid mass there is nothing to look at — and the brush corrects it from there.
	 */
	void FillBackWall(uint8 Variant = 0);

	/** Removes every back wall cell. The maze itself is untouched. */
	void ClearBackWall();

	/** Whether all cells lie in a single plane. */
	bool IsFlat() const;

	// ------------------------------------------------------------------ snapshot

	/**
	 *  Snapshot of the grid. Taken automatically before anything that erases work.
	 *
	 *  Undo is good while the editor is open and useless after a restart. The snapshot
	 *  lives inside the asset itself and survives everything but a forgotten Ctrl+S.
	 */
	UPROPERTY()
	FMazeGrid Snapshot;

	/** What is in the snapshot. Read-only, so that it is visible in the panel. */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FString SnapshotInfo;

	void SaveSnapshot();

	void RestoreSnapshot();

private:
	/** Takes a snapshot before a destructive operation. Does not save an empty grid. */
	void AutoSnapshot(const TCHAR* Reason);

public:

	const FMazeRoomDesc* FindRoom(FName RoomId) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/**
	 *  Undo restores Grid and BakedRoomHashes, but the memo counting baked rooms is a plain
	 *  member, not a UPROPERTY — the transaction does not know it exists. Without this the panel
	 *  keeps reporting the state from before the undo, and the preview is never told to redraw.
	 */
	virtual void PostEditUndo() override;
#endif
};
