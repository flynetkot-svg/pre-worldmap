#pragma once

#include "CoreMinimal.h"
#include "Data/MazeSpawnTypes.h"

struct FMazeGrid;

/**
 *  Which side of a placement's box is pressed against the mass holding it up.
 *
 *  Named rather than derived from the anchor at each use, because a wall placement does not
 *  record which wall: "leans on a wall" is what is stored, and left or right is re-derived
 *  from the grid. Callers want the answer, not the derivation.
 */
enum class EMazeContactFace : uint8
{
	/** Free-standing. Nothing to press against. */
	None,

	/** The bottom, against a floor. */
	MinZ,

	/** The top, against a ceiling. */
	MaxZ,

	/** The left side, against a wall on the left. */
	MinX,

	/** The right side, against a wall on the right. */
	MaxX
};

/**
 *  Whether an object fits somewhere, and which anchor it fits by.
 *
 *  Its own file for one reason: three different callers ask the same question. The brush asks
 *  it under the cursor, the export asks it again to report placements the maze has grown out
 *  from under, and the generator will ask it of every candidate cell. Three copies of this rule
 *  would agree until somebody changed one — the exact failure the mesh and level names were
 *  pulled into a shared helper to avoid.
 */
namespace MazePlacement
{
	/** Every cell of the footprint is passable, and the contact edge of that anchor is mass. */
	MAZEFORGECORE_API bool Fits(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                            const FIntPoint& CellXZ, int32 SliceY, EMazeAnchorKind Anchor);

	/**
	 *  The first anchor the type allows that actually fits here, in order Floor, Ceiling, Wall,
	 *  Free. Returns false when none of them does.
	 *
	 *  Most specific first, because that is the order of intent: something that can stand and is
	 *  standing on a floor is on a floor, not floating in a spot that happens to be free.
	 */
	MAZEFORGECORE_API bool FindAnchor(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                                  const FIntPoint& CellXZ, int32 SliceY,
	                                  EMazeAnchorKind& OutAnchor);

	/**
	 *  Why an object does not fit here, in words, for a message.
	 *
	 *  Deliberately separate from Fits and deliberately slower. Fits has to be cheap because the
	 *  generator will ask it of every candidate cell; this is asked once, about the one placement
	 *  somebody is looking at. It exists because "does not fit" covers five different faults —
	 *  outside the grid, standing inside mass, nothing below, nothing above, nothing beside — and
	 *  the fix is different for every one of them. A refusal that will not say which is a refusal
	 *  you have to guess at, and guessing is what cost an evening the first time this fired.
	 *
	 *  Returns an empty string when the object does fit.
	 */
	MAZEFORGECORE_API FString DescribeMisfit(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                                         const FIntPoint& CellXZ, int32 SliceY);

	/** The anchor to record when nothing fits: the first one the type allows. Never fails. */
	MAZEFORGECORE_API EMazeAnchorKind PreferredAnchor(const FMazeObjectType& Type);

	/**
	 *  Rotation for a fresh placement, from the type's facing mode.
	 *
	 *  Seed rather than FMath::Rand: two runs of the generator over the same maze must produce
	 *  the same world, and a crate facing a different way is still a different world.
	 */
	MAZEFORGECORE_API FRotator ResolveRotation(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                                           const FIntPoint& CellXZ, int32 SliceY,
	                                           EMazeAnchorKind Anchor, int32 Seed);

	/** World transform of a placement, given its type. */
	MAZEFORGECORE_API FVector WorldLocation(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                                        const FMazePlacement& Placement);

	/**
	 *  Which face of the placement's box touches the mass it is anchored to.
	 *
	 *  Public because two callers need the same answer: WorldLocation, to put the origin on
	 *  that surface, and the export, to press the spawned actor's own edge against it.
	 */
	MAZEFORGECORE_API EMazeContactFace ContactFace(const FMazeGrid& Grid,
	                                               const FMazeObjectType& Type,
	                                               const FMazePlacement& Placement);

	/**
	 *  How far to move an actor so that its own edge lands on that surface.
	 *
	 *  Pure arithmetic on a measured bounding box, and deliberately takes no actor: the export
	 *  measures, this decides, the export moves. Zero for a free-standing object, and zero for
	 *  a mesh whose pivot already sits exactly on that edge.
	 */
	MAZEFORGECORE_API FVector ContactSnapDelta(const FBox& ActorBounds, EMazeContactFace Face,
	                                           const FVector& SurfacePoint);

	/** The depth slice a band is placed in: the middle of that band. */
	MAZEFORGECORE_API int32 BandSliceY(const FMazeGrid& Grid, EMazeDepthBand Band);
}
