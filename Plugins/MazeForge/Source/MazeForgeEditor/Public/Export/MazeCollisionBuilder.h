#pragma once

#include "CoreMinimal.h"

struct FKBoxElem;
struct FMazeGrid;
struct FMazeRoomDesc;

/**
 *  Simple room collision made of greedily merged boxes.
 *
 *  It is built for the play band only: the player is locked into the XZ plane and physically
 *  cannot reach the decoration at the edges of the depth axis, so collision across the whole
 *  depth is boxes thrown away for nothing.
 *
 *  The merging runs in three passes: runs along X, then identical runs along Z,
 *  then identical rectangles along Y. Thousands of cells turn into dozens of boxes.
 */
class FMazeCollisionBuilder
{
public:
	/** Returns the number of boxes built. The coordinates are local to Origin. */
	static int32 Build(const FMazeGrid& Grid, const FMazeRoomDesc& Room,
	                   const FVector& Origin, TArray<FKBoxElem>& OutBoxes);
};
