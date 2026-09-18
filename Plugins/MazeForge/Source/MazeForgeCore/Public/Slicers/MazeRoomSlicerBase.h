#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MazeRoomSlicerBase.generated.h"

struct FMazeGrid;
struct FMazeRoomDesc;

/**
 *  Cutting the maze into streaming rooms.
 *
 *  A room is a rectangle in XZ across the whole depth Y. The derived classes decide only
 *  where the borders run; counting the contents, the bounds and the portal graph is done
 *  by the base.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class MAZEFORGECORE_API UMazeRoomSlicerBase : public UObject
{
	GENERATED_BODY()

public:
	/** Throw away rooms that do not have a single solid cell. */
	UPROPERTY(EditAnywhere, Category = "Slicing")
	bool bDiscardEmptyRooms = true;

	/** Entry point: the slicing, then counting the contents, the bounds and the links. */
	void Execute(const FMazeGrid& Grid, TArray<FMazeRoomDesc>& OutRooms) const;

	virtual FText GetDisplayName() const;

protected:
	/** A derived class is only required to fill in RoomId, MinXZ and MaxXZ. */
	virtual void Slice(const FMazeGrid& Grid, TArray<FMazeRoomDesc>& OutRooms) const
		PURE_VIRTUAL(UMazeRoomSlicerBase::Slice, );

	/** Counts SolidCellCount and WorldBounds, throwing away empty rooms if required. */
	static void Finalize(const FMazeGrid& Grid, TArray<FMazeRoomDesc>& InOutRooms, bool bDiscardEmpty);

	/**
	 *  The portal graph: two rooms are linked if there is a passable pair of cells in the
	 *  play band across their shared face. Only the 4 side faces are scanned — along Y the
	 *  rooms do not border each other.
	 *
	 *  This gives the runtime graph proximity instead of Euclidean proximity: in a maze
	 *  with thick walls a room next door through the air can be a hundred metres of travel.
	 */
	static void BuildPortalGraph(const FMazeGrid& Grid, TArray<FMazeRoomDesc>& InOutRooms);
};
