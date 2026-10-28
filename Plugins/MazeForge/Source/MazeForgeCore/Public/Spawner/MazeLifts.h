#pragma once

#include "CoreMinimal.h"
#include "Data/MazeSpawnTypes.h"

class UMazeObjectLibrary;

/** Which lift a call terminal calls, and to which of its stops. */
struct FMazeLiftBinding
{
	FName LiftId;
	int32 Stop = INDEX_NONE;
};

/**
 *  Lifts and their terminals, for the export and the editor alike.
 *
 *  A lift is a placement of a Platform type with Climb Cells: its lower stop is its own cell row,
 *  its upper one Climb Cells above.
 */
namespace MazeLifts
{
	MAZEFORGECORE_API bool IsLift(const UMazeObjectLibrary* Library, const FMazePlacement& Placement);
	MAZEFORGECORE_API bool IsCall(const UMazeObjectLibrary* Library, const FMazePlacement& Placement);

	/** The lift's name: its Lift Id, or one made from its cell — stable for as long as it stays put. */
	MAZEFORGECORE_API FName ResolveId(const FMazePlacement& Lift);

	/**
	 *  The lift a terminal calls. With a Lift Id: the lift with that name, and the stop nearest the
	 *  terminal's level. Without: the nearest lift along X with a stop within MaxOffCells of the
	 *  terminal's level. False if there is none.
	 */
	MAZEFORGECORE_API bool BindCall(const TArray<FMazePlacement>& Placements, const UMazeObjectLibrary* Library,
	                                const FMazePlacement& Call, FMazeLiftBinding& Out, int32 MaxOffCells = 1);
}
