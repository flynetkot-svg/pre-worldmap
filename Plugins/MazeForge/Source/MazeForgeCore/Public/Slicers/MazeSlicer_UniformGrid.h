#pragma once

#include "CoreMinimal.h"
#include "Slicers/MazeRoomSlicerBase.h"
#include "MazeSlicer_UniformGrid.generated.h"

/**
 *  A rectangle of lattice cells that becomes one room.
 *
 *  Indices of the slicing lattice and not grid cells, deliberately. A merge has to mean the
 *  same thing after the maze is redrawn, and cell coordinates would slide out from under it
 *  the moment Room Size changed. Max is INCLUSIVE, because a one-cell merge is Min == Max and
 *  that is what a single click produces.
 *
 *  Rectangles only, and that is the data model rather than a simplification worth lifting
 *  later. FMazeRoomDesc is a MinXZ/MaxXZ pair and ContainsXZ is two comparisons; an L-shaped
 *  room has nowhere to be stored, and nothing at runtime could answer "which room is this
 *  cell in" for one.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeRoomMerge
{
	GENERATED_BODY()

	/** Lowest lattice cell of the rectangle, inclusive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slicing")
	FIntPoint MinIndex = FIntPoint::ZeroValue;

	/** Highest lattice cell of the rectangle. Inclusive — see the note above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slicing")
	FIntPoint MaxIndex = FIntPoint::ZeroValue;

	bool Contains(const FIntPoint& Index) const
	{
		return Index.X >= MinIndex.X && Index.X <= MaxIndex.X
			&& Index.Y >= MinIndex.Y && Index.Y <= MaxIndex.Y;
	}

	bool Intersects(const FMazeRoomMerge& Other) const
	{
		return MinIndex.X <= Other.MaxIndex.X && MaxIndex.X >= Other.MinIndex.X
			&& MinIndex.Y <= Other.MaxIndex.Y && MaxIndex.Y >= Other.MinIndex.Y;
	}

	int32 CellCount() const
	{
		return IsValid() ? (MaxIndex.X - MinIndex.X + 1) * (MaxIndex.Y - MinIndex.Y + 1) : 0;
	}

	bool IsValid() const { return MaxIndex.X >= MinIndex.X && MaxIndex.Y >= MinIndex.Y; }

	FString Describe() const
	{
		return FString::Printf(TEXT("%d x %d cells from [%d, %d]"),
			MaxIndex.X - MinIndex.X + 1, MaxIndex.Y - MinIndex.Y + 1, MinIndex.X, MinIndex.Y);
	}
};

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

	/**
	 *  Lattice cells fused into single rooms. Empty means the plain lattice.
	 *
	 *  What this is for: an object spanning several rooms lives in exactly one level and
	 *  vanishes whole when that level unloads — a lift shaft, a long ladder, a rope across a
	 *  gap. Merging the cells it crosses makes it one room's business, and the problem stops
	 *  existing instead of being managed. Cheaper than any mechanism for keeping neighbours
	 *  alive, and it cannot fall out of step with the object, because there is nothing to
	 *  keep in step.
	 *
	 *  The price runs the other way: a merged room loads as one chunk. A shaft is thin and
	 *  nearly empty, so merging it costs almost nothing; merging half the map throws the
	 *  streaming away. The slicing report is where that shows up.
	 *
	 *  Merges never overlap — AddMerge absorbs anything a new one touches — so the question
	 *  "which merge owns this cell" has exactly one answer and needs no tie-breaking rule.
	 */
	UPROPERTY(EditAnywhere, Category = "Slicing")
	TArray<FMazeRoomMerge> Merges;

	/** What the slicing steps by: the room size, never below one cell. */
	FIntPoint LatticeStep() const;

	/**
	 *  Where the lattice starts, in grid cells. May be negative.
	 *
	 *  Negative on purpose: starting at the lattice node nearest zero is what stops an Origin
	 *  shift from cutting the edge of the map off.
	 */
	FIntPoint LatticeStart() const;

	/**
	 *  Which lattice cell a grid cell falls into.
	 *
	 *  The one place this arithmetic lives. The editor needs it to turn a drag into indices
	 *  and the slicing needs it to cut; two copies of it would be two answers to the same
	 *  question, which is the failure this plugin keeps tripping over.
	 */
	FIntPoint CellToLattice(const FIntPoint& CellXZ) const;

	/** The grid cells one lattice cell covers, clamped to the grid. Max exclusive. */
	bool LatticeToCells(const FIntPoint& Index, const FIntPoint& GridSizeXZ,
	                    FIntPoint& OutMinXZ, FIntPoint& OutMaxXZ) const;

	/** The merge owning this lattice cell, or INDEX_NONE. */
	int32 FindMerge(const FIntPoint& Index) const;

	/**
	 *  Adds a merge, swallowing every merge it touches. Returns how many it absorbed.
	 *
	 *  Absorbing rather than refusing: two overlapping merges would have to be resolved by
	 *  some rule the designer cannot see, and the result of merging into an existing room IS
	 *  the larger room. What it must not do is happen quietly — the caller reports it.
	 */
	int32 AddMerge(const FMazeRoomMerge& Merge);

	/** Drops every merge meeting this rectangle. Returns how many went. */
	int32 RemoveMergesIn(const FMazeRoomMerge& Area);

	virtual FText GetDisplayName() const override;

#if WITH_EDITOR
	using Super::PreEditChange;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;

	/**
	 *  Guards the merges against a change of Room Size or Origin.
	 *
	 *  Merges are stored as lattice cell numbers. A new step or offset gives the same numbers
	 *  different places on the map, so every merged room would silently land somewhere else —
	 *  hours of colouring turned into nonsense by one typed digit. With merges present, the
	 *  change asks: keep the new lattice and clear the merges, or keep the merges and put the
	 *  old lattice back.
	 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void Slice(const FMazeGrid& Grid, TArray<FMazeRoomDesc>& OutRooms) const override;

private:
#if WITH_EDITORONLY_DATA
	/** The lattice as it was before the edit in progress. Bridges PreEditChange and PostEditChange. */
	FIntPoint RoomSizeBeforeEdit = FIntPoint::ZeroValue;
	FIntPoint OriginBeforeEdit = FIntPoint::ZeroValue;
	bool bLatticeEditPending = false;
#endif
};
