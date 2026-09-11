#pragma once

#include "CoreMinimal.h"
#include "Slicers/MazeRoomSlicerBase.h"
#include "MazeSlicer_UniformGrid.generated.h"

/** Automatic slicing with a regular grid: one click and the whole map is cut up. */
UCLASS(DisplayName = "Uniform Grid")
class MAZEFORGECORE_API UMazeSlicer_UniformGrid : public UMazeRoomSlicerBase
{
	GENERATED_BODY()

public:
	/** Room size in cells: X along the level, Y along world Z (the height). */
	UPROPERTY(EditAnywhere, Category = "Slicing", meta = (ClampMin = "1"))
	FIntPoint RoomSizeXZ = FIntPoint(32, 32);

	/** Shift of the slicing lattice in cells. */
	UPROPERTY(EditAnywhere, Category = "Slicing")
	FIntPoint OriginXZ = FIntPoint::ZeroValue;

	virtual FText GetDisplayName() const override;

protected:
	virtual void Slice(const FMazeGrid& Grid, TArray<FMazeRoomDesc>& OutRooms) const override;
};
