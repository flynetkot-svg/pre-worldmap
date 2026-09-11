#pragma once

#include "CoreMinimal.h"

class UMazeGridAsset;
class UWorld;

/**
 *  Attaching rooms to the current persistent level.
 *
 *  Splitting this out of the export into a separate action is deliberate: the export creates
 *  assets and does not touch anyone else's level, and modifying MainLevel always stays a
 *  conscious step by the designer. Otherwise every re-export would rewrite the persistent map
 *  and it would conflict in version control for the whole team.
 */
class FMazeLevelAttacher
{
public:
	/** Adds the room levels to the open persistent level. Returns how many were added. */
	static int32 AttachRooms(UMazeGridAsset* Asset);

	/** Removes everything that lives in the maze level folder from the persistent level. */
	static int32 DetachRooms(UMazeGridAsset* Asset);

private:
	/**
	 *  Deletes the Outliner folders this maze created, once its levels are out of the world.
	 *
	 *  Only its own: the per-room folders and, when the maze is named, its compartment. The
	 *  shared root ("Levels" by default) is never touched — other things live there.
	 */
	static int32 RemoveOutlinerFolders(UWorld* World, const UMazeGridAsset* Asset);
};
