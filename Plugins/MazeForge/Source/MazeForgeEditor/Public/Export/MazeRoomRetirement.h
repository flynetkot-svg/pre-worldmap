#pragma once

#include "CoreMinimal.h"

class UMazeGridAsset;
class UMazeWorldManifest;

/** What taking abolished rooms off the map did, during Apply. */
struct FMazeRetirementReport
{
	/** Rooms that were in the last build and are not in this slicing. */
	int32 Rooms = 0;

	int32 Detached = 0;
	int32 FoldersRemoved = 0;

	/**
	 *  Outliner folders of rooms that no longer exist, left behind by an earlier build and swept
	 *  up now: a room retired before objects had their own subfolder, or before retirement
	 *  existed at all. Counted apart from FoldersRemoved, which belongs to this pass's rooms.
	 */
	int32 LeftoverFolders = 0;

	/** Levels of rooms that no longer exist still attached to the map by an earlier build. */
	int32 LeftoverLevels = 0;

	/** Retired rooms whose level still holds actors the export did not create. */
	TArray<FString> RoomsHoldingDecor;

	TArray<FString> RoomNames;

	bool IsEmpty() const { return Rooms == 0; }

	FString ToString() const;
};

/** What a Move Stale Rooms To Deprecated pass did. */
struct FMazeDeprecationReport
{
	int32 StaleLevels = 0;
	int32 StaleMeshes = 0;

	int32 LevelsMoved = 0;
	int32 MeshesMoved = 0;

	/** Moved packages unloaded again before the next batch. */
	int32 Unloaded = 0;

	/** Assets that could not be moved. They are left exactly where they were. */
	int32 Failed = 0;

	int32 Batches = 0;
	double Seconds = 0.0;
	bool bCancelled = false;

	/** Moved levels that held actors the export did not create. */
	TArray<FString> LevelsHoldingDecor;

	FString ToString() const;
};

/**
 *  Rooms a re-slicing has abolished: off the map at once, out of the way on request.
 *
 *  Merging rooms, or changing Room Size, does not edit the old rooms — it stops producing
 *  them. The manifest is rewritten from the new slicing, so the game never loads them again,
 *  but their levels would stay attached to the persistent map as duplicate geometry standing
 *  inside the rooms that replaced them. Taking them off is cheap and happens during Apply.
 *
 *  Moving their assets aside is not cheap, and it used to happen during Apply as well. On the
 *  first real map it was nine minutes of a fourteen-minute build — 496 seconds of it spent
 *  inside the engine's RenameAssets before it even began to save, fixing up references to
 *  assets that nothing live refers to. That cost cannot be switched off from outside, so it
 *  is paid only when asked for: a separate button, run when the designer wants the folder
 *  tidy, and never as the price of seeing a change.
 *
 *  Nothing is ever deleted. Moved assets go into a Deprecated folder beside the live ones,
 *  so an accident costs a drag back rather than a rebuild, and hand-placed decor in a
 *  retired level travels with it instead of vanishing.
 */
class FMazeRoomRetirement
{
public:
	/**
	 *  The room ids a manifest currently lists.
	 *
	 *  Taken BEFORE the export rewrites it — afterwards there is nothing left to compare
	 *  against, which is why this was invisible for so long.
	 */
	static TArray<FName> SnapshotRoomIds(const UMazeWorldManifest* Manifest);

	/**
	 *  Takes every room in the snapshot that the new slicing no longer produces off the map.
	 *  Detaches its level and clears its Outliner folders; moves nothing.
	 */
	static FMazeRetirementReport Retire(const UMazeGridAsset* Asset,
	                                    const TArray<FName>& PreviousRoomIds);

	/**
	 *  Counts the room levels and meshes in this maze's folders that its manifest does not use.
	 *
	 *  Found by comparing the folders with the manifest rather than by remembering what was
	 *  retired, so it also catches what a crash, a cancelled export or an older version of the
	 *  plugin left behind. Returns false if the maze has no manifest to compare against.
	 */
	static bool CountStale(const UMazeGridAsset* Asset, int32& OutLevels, int32& OutMeshes);

	/** Moves everything CountStale finds into Deprecated, in batches, with a progress bar. */
	static FMazeDeprecationReport MoveStaleToDeprecated(const UMazeGridAsset* Asset);
};
