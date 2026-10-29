#pragma once

#include "CoreMinimal.h"
#include "Data/MazeSpawnTypes.h"

struct FMazeGrid;
class UMazeObjectLibrary;

/**
 *  Sliding doors, for the Door tool and the export alike.
 *
 *  A door is a placement of a type with Sliding Door set: its cell is its bottom-left, it is
 *  Footprint Cells X wide and Height Cells tall, and it fills the Play band's depth.
 */
namespace MazeDoors
{
	MAZEFORGECORE_API bool IsDoor(const UMazeObjectLibrary* Library, const FMazePlacement& Placement);

	/** The bottom a click at Cell takes: the floor at or below it within Tolerance, or the first free row above mass. */
	MAZEFORGECORE_API bool PickBottom(const FMazeGrid& Grid, int32 SliceY, const FIntPoint& Cell, int32 Tolerance,
	                                  int32& OutBottom);

	/** The top a click at Z takes, over Bottom in column X: the ceiling within Tolerance, or the row clicked. */
	MAZEFORGECORE_API int32 PickTop(const FMazeGrid& Grid, int32 SliceY, int32 X, int32 Bottom, int32 Z, int32 Tolerance);

	/** Whether Width columns from X are free from Bottom to Top inclusive; the reason if not. */
	MAZEFORGECORE_API bool IsClear(const FMazeGrid& Grid, int32 SliceY, int32 X, int32 Width, int32 Bottom, int32 Top,
	                               FString* OutReason = nullptr);

	/** Floor to ceiling from Bottom in column X, in cells (at least 1, at most 64). */
	MAZEFORGECORE_API int32 MeasureHeight(const FMazeGrid& Grid, int32 SliceY, int32 X, int32 Bottom);

	/** The cells a door covers: Width wide, Height Cells tall. Max exclusive. */
	MAZEFORGECORE_API FIntRect Rect(const FMazePlacement& Door, int32 Width);
}
