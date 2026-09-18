#pragma once

#include "CoreMinimal.h"

class UMazeGridAsset;

/** The result of an export — what we show the designer and put into the log. */
struct FMazeExportReport
{
	int32 Rooms = 0;
	int32 Levels = 0;
	int32 Meshes = 0;

	/** Of those, taken ready-made from disk without rebuilding the geometry. */
	int32 ReusedMeshes = 0;

	int32 Triangles = 0;
	int32 CollisionBoxes = 0;

	/** Objects spawned from the spawn asset. */
	int32 Objects = 0;

	/**
	 *  Of those, ones whose stored anchor no longer held and that found another one.
	 *
	 *  Not a failure — a crate that was standing on a floor and is now leaning on a wall is
	 *  still in the world. It is reported because its facing may have turned with it.
	 */
	int32 ReanchoredObjects = 0;

	/**
	 *  Of those, ones whose measured bounds did not already meet their anchor surface.
	 *
	 *  Expected to be most of them, and not a complaint: it is the number of props whose pivot
	 *  is somewhere other than the face they hang by. A sudden zero, on the other hand, means
	 *  the measuring found nothing — worth a look before wondering why lamps are in ceilings.
	 */
	int32 SnappedObjects = 0;

	/** Placements that no longer fit anywhere and were NOT spawned. */
	int32 SkippedObjects = 0;

	/** Placements whose type is missing from the library, or has no actor class set. */
	int32 OrphanObjects = 0;

	/**
	 *  Generated objects that had been resized by hand, and whose resizing this export threw
	 *  away.
	 *
	 *  The export owns every actor it tagged: each one is destroyed and made again from the
	 *  library, which is what lets a maze be rebuilt at all. The cost is that anything done to
	 *  such an actor in the level is undone by the next Apply Changes, silently — a door whose
	 *  trigger volume had been stretched to fit came back the size of a crate and stopped
	 *  catching anybody, and nothing anywhere said why.
	 *
	 *  Scale only, because it is the one thing the export never sets on an object and therefore
	 *  the one thing that can only have come from a hand.
	 */
	int32 HandResizedActors = 0;

	/**
	 *  Objects asking to face away from a wall that are not standing against one.
	 *
	 *  Not a failure — they get their fixed angle and are spawned. It is counted because the
	 *  facing mode then does nothing at all, and a designer who chose it is owed the news that
	 *  it had no effect rather than being left to wonder why everything points one way.
	 */
	int32 FacingModeIgnored = 0;

	int32 ReplacedActors = 0;
	int32 PreservedActors = 0;
	int32 FailedPackages = 0;

	/** How many packages were unloaded from memory over the course of the export. */
	int32 FlushedPackages = 0;

	double Seconds = 0.0;

	FString ToString() const;
};

/**
 *  Step 5 of the pipeline: building the room levels and the manifest.
 *
 *  Geometry is not built here if step 4 (FMazeBakery) already built it and the grid has not
 *  been edited since: the meshes are taken from disk by name. If a baked mesh is missing or
 *  the revision has drifted, the export builds what is missing itself — so that a single
 *  button can be pressed without baking first.
 *
 *  The persistent level is deliberately left alone: the streaming entries are created by the
 *  pool at runtime from the manifest, so MainLevel is not modified and does not conflict
 *  with someone else's edits.
 */
class FMazeLevelExporter
{
public:
	static bool ExportRooms(UMazeGridAsset* Asset, FMazeExportReport& OutReport);
};
