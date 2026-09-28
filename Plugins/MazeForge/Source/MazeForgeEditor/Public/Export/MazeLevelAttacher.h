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

	/**
	 *  Makes the level of the room holding this cell the one new actors are created in.
	 *
	 *  The whole of the hand-decoration problem, and it needs no new data. A room IS a level,
	 *  and which level an actor is saved in is the only binding the streaming reads. A
	 *  property or an interface saying "I belong to R_003_002" would be a second identity to
	 *  keep in step with the first, and it would be free to lie: an actor sitting in the
	 *  persistent level while claiming a room is a statement nothing checks.
	 *
	 *  So the answer is to point the editor at the right level before the actor exists — a
	 *  thing the editor already does. It just takes finding that room among two hundred in the
	 *  Levels panel, every single time, and getting it wrong is silent.
	 *
	 *  Returns the level's name on success and an empty string on every failure, each of which
	 *  says in the log what to do about it.
	 */
	static FString MakeRoomLevelCurrent(const UMazeGridAsset* Asset, const FIntPoint& CellXZ);

	/** The name of the level new actors are being created in. Empty when there is no world. */
	static FString GetCurrentLevelName();

	/** Whether that level is the persistent one — where decor never streams. */
	static bool IsPersistentLevelCurrent();

	/**
	 *  Shows or hides every room level of this maze at once. Returns how many changed.
	 *
	 *  For looking at the slicing. Once the rooms are attached, the viewport shows the built
	 *  geometry, and the room frames and merge hatching are thin lines drawn on top of a solid
	 *  maze — the one view in which the thing being edited is the hardest to see. Hiding the
	 *  levels puts the drawing back on screen instead, which is what the frames belong to.
	 *
	 *  A toggle rather than two buttons, and the direction is read off the levels themselves:
	 *  a remembered flag would be a second opinion about a state the Levels panel can change
	 *  without telling anyone.
	 */
	static int32 SetRoomLevelsVisible(const UMazeGridAsset* Asset, bool bVisible);

	/** Whether any room level of this maze is currently shown in the editor. */
	static bool AreRoomLevelsVisible(const UMazeGridAsset* Asset);

private:
	/**
	 *  Deletes the Outliner folders this maze created, once its levels are out of the world.
	 *
	 *  Only its own: the per-room folders and, when the maze is named, its compartment. The
	 *  shared root ("Levels" by default) is never touched — other things live there.
	 */
	static int32 RemoveOutlinerFolders(UWorld* World, const UMazeGridAsset* Asset);
};
