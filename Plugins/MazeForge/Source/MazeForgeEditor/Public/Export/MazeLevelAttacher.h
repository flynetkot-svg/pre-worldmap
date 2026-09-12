#pragma once

#include "CoreMinimal.h"

class UMazeGridAsset;
class UMazeWorldGraph;
class UMazeWorldManifest;
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

	/**
	 *  The same, from a manifest alone.
	 *
	 *  A manifest is all the attaching ever needed — the list of room levels — and taking it
	 *  directly is what lets a whole world be attached without opening, and re-baking, ten grid
	 *  assets to get at ten lists.
	 */
	static int32 AttachRooms(const UMazeWorldManifest* Manifest);

	/**
	 *  Puts every maze the world graph names onto the open persistent level.
	 *
	 *  Building is not involved: nothing is generated, nothing is exported, no mesh is touched.
	 *  This only makes the map agree with the graph, which is the state a fresh clone, a hand
	 *  detach, or a newly built maze leaves it out of. Returns how many levels were added.
	 */
	static int32 AttachWorld(const UMazeWorldGraph* Graph);

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
