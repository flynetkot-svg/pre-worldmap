#pragma once

#include "CoreMinimal.h"

class UMazeGridAsset;

/** What one Bind Decor To Rooms pass did. Counted in root actors; attached children travel along. */
struct FMazeDecorBindReport
{
	/** Taken from the persistent level into the level of the room they stand in. */
	int32 FromMain = 0;

	/** Already in a room level, standing in another room since the slicing changed. */
	int32 BetweenRooms = 0;

	/** Taken out of the levels of rooms a re-slicing abolished. */
	int32 FromRetired = 0;

	/** Of those, ones standing in no room any more: put into the persistent level so they are not lost. */
	int32 RetiredToMain = 0;

	/** Already in the level of their room. Not touched. */
	int32 AlreadyHome = 0;

	/**
	 *  Standing in a room but kept where they are: by class or tag (Build Settings, Decor), or
	 *  because they implement IMazeKeepInPersistentLevel and answer yes.
	 */
	int32 Kept = 0;

	/** In a room level but standing outside every room. Left where they are. */
	int32 OutsideRooms = 0;

	/** Actors, children included, that could not be moved. They stay where they were. */
	int32 Failed = 0;

	int32 SavedPackages = 0;
	double Seconds = 0.0;
	bool bCancelled = false;

	int32 Planned() const { return FromMain + BetweenRooms + FromRetired; }
	FString ToString() const;
};

/**
 *  Moves hand-placed decor into the levels of the rooms it stands in.
 *
 *  The decor is dressed in the persistent level, with the whole maze in view, and that is the
 *  one level that never streams: everything left in it is loaded for the entire game. A room
 *  IS a level, so binding a prop to a room means moving it into that level — no property says
 *  "I belong to R_003_002", which would be a second identity free to disagree with the first.
 *
 *  The same press re-homes decor after a re-slicing: actors in a room level that now stand in
 *  another room move there, and the levels of abolished rooms are opened long enough to take
 *  their decor out. Apply never moves decor; it only keeps it when rebuilding a room.
 *
 *  Which room: the centre of the actor's bounds, in XZ, against the room bounds the manifest
 *  lists. Depth does not matter — a backdrop far behind the play band belongs to its room.
 *
 *  What moves: hand-placed actors (MazeExport::IsHandPlaced) that are not attached to another
 *  actor, with everything attached to them. Never Info actors, never actors implementing
 *  IMazeKeepInPersistentLevel, never the classes or the tag Build Settings keep in the
 *  persistent level.
 *
 *  Asks once with the counts, then saves every level it touched, the persistent one included:
 *  a room level saved with the decor next to a persistent level still holding it would put
 *  every prop into the game twice. The move goes through the editor's own Move To Level, so
 *  references between actors in different levels do not survive it, as they would not by hand.
 */
class FMazeDecorBinder
{
public:
	static FMazeDecorBindReport Bind(const UMazeGridAsset* Asset);
};
