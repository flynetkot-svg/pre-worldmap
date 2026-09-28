#include "Slicers/MazeSlicer_UniformGrid.h"

#include "Data/MazeGrid.h"
#include "Data/MazeRoomDesc.h"

#define LOCTEXT_NAMESPACE "MazeForge"

FText UMazeSlicer_UniformGrid::GetDisplayName() const
{
	return LOCTEXT("SlicerUniform", "Uniform Grid");
}

FIntPoint UMazeSlicer_UniformGrid::LatticeStep() const
{
	return FIntPoint(FMath::Max(1, RoomSizeXZ.X), FMath::Max(1, RoomSizeXZ.Y));
}

FIntPoint UMazeSlicer_UniformGrid::LatticeStart() const
{
	const FIntPoint Step = LatticeStep();

	// We start at the lattice node nearest to zero so that the Origin shift does not
	// cut off the edge of the map.
	int32 StartX = OriginXZ.X % Step.X;
	int32 StartZ = OriginXZ.Y % Step.Y;
	if (StartX > 0) { StartX -= Step.X; }
	if (StartZ > 0) { StartZ -= Step.Y; }

	return FIntPoint(StartX, StartZ);
}

FIntPoint UMazeSlicer_UniformGrid::CellToLattice(const FIntPoint& CellXZ) const
{
	const FIntPoint Step = LatticeStep();
	const FIntPoint Start = LatticeStart();

	// Rounded down and not truncated. Start can be negative, so a cell near the left edge
	// gives a negative numerator, and C integer division truncates towards zero — index 0
	// would then cover two columns of the lattice instead of one, and every merge drawn near
	// that edge would land one cell off with nothing to show for it.
	return FIntPoint(FMath::DivideAndRoundDown(CellXZ.X - Start.X, Step.X),
	                 FMath::DivideAndRoundDown(CellXZ.Y - Start.Y, Step.Y));
}

bool UMazeSlicer_UniformGrid::LatticeToCells(const FIntPoint& Index, const FIntPoint& GridSizeXZ,
                                             FIntPoint& OutMinXZ, FIntPoint& OutMaxXZ) const
{
	const FIntPoint Step = LatticeStep();
	const FIntPoint Start = LatticeStart();

	OutMinXZ = FIntPoint(FMath::Max(0, Start.X + Index.X * Step.X),
	                     FMath::Max(0, Start.Y + Index.Y * Step.Y));
	OutMaxXZ = FIntPoint(FMath::Min(GridSizeXZ.X, Start.X + (Index.X + 1) * Step.X),
	                     FMath::Min(GridSizeXZ.Y, Start.Y + (Index.Y + 1) * Step.Y));

	return OutMaxXZ.X > OutMinXZ.X && OutMaxXZ.Y > OutMinXZ.Y;
}

int32 UMazeSlicer_UniformGrid::FindMerge(const FIntPoint& Index) const
{
	for (int32 MergeIndex = 0; MergeIndex < Merges.Num(); ++MergeIndex)
	{
		if (Merges[MergeIndex].Contains(Index))
		{
			return MergeIndex;
		}
	}

	return INDEX_NONE;
}

int32 UMazeSlicer_UniformGrid::AddMerge(const FMazeRoomMerge& Merge)
{
	if (!Merge.IsValid())
	{
		return 0;
	}

	FMazeRoomMerge Grown = Merge;
	int32 Absorbed = 0;

	// Iterated rather than done in one pass: swallowing a merge grows the rectangle, and the
	// grown rectangle can reach a third merge the original never touched. Stopping after one
	// pass would leave two merges overlapping, and the no-overlap invariant is what lets
	// FindMerge return the first hit without a tie-breaking rule.
	bool bChanged = true;
	while (bChanged)
	{
		bChanged = false;

		for (int32 Index = Merges.Num() - 1; Index >= 0; --Index)
		{
			if (!Grown.Intersects(Merges[Index]))
			{
				continue;
			}

			Grown.MinIndex = FIntPoint(FMath::Min(Grown.MinIndex.X, Merges[Index].MinIndex.X),
			                           FMath::Min(Grown.MinIndex.Y, Merges[Index].MinIndex.Y));
			Grown.MaxIndex = FIntPoint(FMath::Max(Grown.MaxIndex.X, Merges[Index].MaxIndex.X),
			                           FMath::Max(Grown.MaxIndex.Y, Merges[Index].MaxIndex.Y));

			Merges.RemoveAt(Index);
			++Absorbed;
			bChanged = true;
		}
	}

	Merges.Add(Grown);
	return Absorbed;
}

int32 UMazeSlicer_UniformGrid::RemoveMergesIn(const FMazeRoomMerge& Area)
{
	if (!Area.IsValid())
	{
		return 0;
	}

	// Touching is enough to remove, and a merge is never cut down to the part outside the
	// drag. Splitting one would have to invent where the pieces go, and a piece of a merge is
	// not something the designer drew — the whole thing is. Un-merging the shaft and drawing
	// the part you wanted back is one gesture; recovering from a rectangle the tool invented
	// is not.
	return Merges.RemoveAll([&Area](const FMazeRoomMerge& Merge)
	{
		return Merge.Intersects(Area);
	});
}

void UMazeSlicer_UniformGrid::Slice(const FMazeGrid& Grid, TArray<FMazeRoomDesc>& OutRooms) const
{
	const FIntPoint Size = Grid.SizeXZ;
	const FIntPoint Step = LatticeStep();
	const FIntPoint Start = LatticeStart();

	// Which room each merge has already produced. The lattice is walked cell by cell, so the
	// second cell of a merge has to find the room the first one made rather than add another.
	TMap<int32, int32> MergeToRoom;

	int32 IndexX = 0;
	for (int32 X = Start.X; X < Size.X; X += Step.X, ++IndexX)
	{
		int32 IndexZ = 0;
		for (int32 Z = Start.Y; Z < Size.Y; Z += Step.Y, ++IndexZ)
		{
			const FIntPoint CellMin(FMath::Max(0, X), FMath::Max(0, Z));
			const FIntPoint CellMax(FMath::Min(Size.X, X + Step.X), FMath::Min(Size.Y, Z + Step.Y));

			if (CellMax.X <= CellMin.X || CellMax.Y <= CellMin.Y)
			{
				continue;
			}

			const int32 MergeIndex = FindMerge(FIntPoint(IndexX, IndexZ));

			if (MergeIndex == INDEX_NONE)
			{
				FMazeRoomDesc Room;
				Room.MinXZ = CellMin;
				Room.MaxXZ = CellMax;
				Room.RoomId = FName(*FString::Printf(TEXT("R_%03d_%03d"), IndexX, IndexZ));
				OutRooms.Add(MoveTemp(Room));
				continue;
			}

			if (const int32* Existing = MergeToRoom.Find(MergeIndex))
			{
				FMazeRoomDesc& Room = OutRooms[*Existing];
				Room.MinXZ = FIntPoint(FMath::Min(Room.MinXZ.X, CellMin.X),
				                       FMath::Min(Room.MinXZ.Y, CellMin.Y));
				Room.MaxXZ = FIntPoint(FMath::Max(Room.MaxXZ.X, CellMax.X),
				                       FMath::Max(Room.MaxXZ.Y, CellMax.Y));
				continue;
			}

			FMazeRoomDesc Room;
			Room.MinXZ = CellMin;
			Room.MaxXZ = CellMax;

			// The id comes from the merge's own lowest cell, not from the first cell the scan
			// happened to reach, so it does not depend on the order of the walk. The naming
			// cell is always inside the merge and so never produces a room of its own, which
			// is what stops the name colliding with an unmerged room's.
			//
			// What this rule does NOT promise: that the name belonged to a room before. Empty
			// cells are discarded, so a merge whose lowest corner lies over empty space is
			// named after a cell that was never a room, and every level inside the merge is
			// orphaned rather than one of them being kept. Deliberate — a name that chased
			// whichever cell happened to have content would change whenever the maze was
			// redrawn, which is worse. The editor warns when it happens.
			Room.RoomId = FName(*FString::Printf(TEXT("R_%03d_%03d"),
				Merges[MergeIndex].MinIndex.X, Merges[MergeIndex].MinIndex.Y));

			MergeToRoom.Add(MergeIndex, OutRooms.Num());
			OutRooms.Add(MoveTemp(Room));
		}
	}
}

#undef LOCTEXT_NAMESPACE
