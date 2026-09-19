#include "Assets/MazeSpawnAsset.h"

#include "Assets/MazeObjectLibrary.h"
#include "MazeForgeCore.h"

int32 UMazeSpawnAsset::Add(const FMazePlacement& Placement)
{
#if WITH_EDITOR
	Modify();
#endif

	// Deliberately no id here. Drawing creates nothing in the world, so there is nothing yet to
	// identify; the export hands out the numbers. See AssignId.
	const int32 Index = Placements.Add(Placement);
	Placements[Index].Id = 0;

	NotifySpawnsChanged();
	return Index;
}

int32 UMazeSpawnAsset::AddBatch(const TArray<FMazePlacement>& NewPlacements)
{
	if (NewPlacements.Num() == 0)
	{
		return 0;
	}

#if WITH_EDITOR
	// One Modify for the lot. See the header: the alternative is an undo history the designer
	// has to walk back one crate at a time.
	Modify();
#endif

	Placements.Reserve(Placements.Num() + NewPlacements.Num());

	for (const FMazePlacement& Placement : NewPlacements)
	{
		const int32 Index = Placements.Add(Placement);

		// Same rule as Add, and it has to be repeated rather than trusted: a caller building
		// placements by hand can leave anything in this field, and an id that was not handed
		// out by AssignId would collide with one that was.
		Placements[Index].Id = 0;
	}

	NotifySpawnsChanged();
	return NewPlacements.Num();
}

void UMazeSpawnAsset::RemoveAt(int32 Index)
{
	if (!Placements.IsValidIndex(Index))
	{
		return;
	}

#if WITH_EDITOR
	Modify();
#endif

	// RemoveAt and not RemoveAtSwap: the order is what "topmost" means to the eraser, and
	// swapping the tail into the hole would quietly reorder what the next click picks up.
	Placements.RemoveAt(Index);
	NotifySpawnsChanged();
}

int32 UMazeSpawnAsset::RemoveGenerated()
{
	const int32 Before = Placements.Num();
	if (Before == 0)
	{
		return 0;
	}

#if WITH_EDITOR
	Modify();
#endif

	// RemoveAll and not a reverse loop of RemoveAt: order among the survivors is preserved
	// either way, and one pass beats N shifts of the tail.
	const int32 Removed = Placements.RemoveAll([](const FMazePlacement& Placement)
	{
		return Placement.bGenerated;
	});

	if (Removed == 0)
	{
		return 0;
	}

	// NextId is not rolled back, for the same reason ClearAll does not roll it back: a number
	// that has been handed out may be named by a save on somebody's disk.
	UE_LOG(LogMazeForge, Log,
		TEXT("%s: %d generated placements removed, %d placed by hand kept."),
		*GetName(), Removed, Placements.Num());

	NotifySpawnsChanged();
	return Removed;
}

int32 UMazeSpawnAsset::FindAtCell(const UMazeObjectLibrary* InLibrary, const FIntPoint& CellXZ) const
{
	// One cell is a one-by-one rectangle. Written this way rather than as its own loop so that
	// the two questions can never drift apart: they are the same question at two sizes, and a
	// copy of this arithmetic is a copy that will one day disagree.
	return FindOverlapping(InLibrary, CellXZ, FIntPoint(1, 1));
}

int32 UMazeSpawnAsset::FindOverlapping(const UMazeObjectLibrary* InLibrary,
                                       const FIntPoint& MinXZ, const FIntPoint& ExtentXZ) const
{
	const FIntPoint Max(MinXZ.X + FMath::Max(1, ExtentXZ.X),
	                    MinXZ.Y + FMath::Max(1, ExtentXZ.Y));

	// Backwards: the last one added is the one on top, and the one the eraser should take first.
	for (int32 Index = Placements.Num() - 1; Index >= 0; --Index)
	{
		const FMazePlacement& Placement = Placements[Index];

		FIntPoint Footprint(1, 1);
		if (InLibrary)
		{
			if (const FMazeObjectType* Type = InLibrary->FindType(Placement.TypeId))
			{
				Footprint = Type->FootprintCells;
			}
		}

		const FIntPoint OtherMax = Placement.MaxCellXZ(Footprint);

		// Two rectangles miss each other when one ends before the other begins, on either axis.
		// Maxima are exclusive on both sides, so touching edges are not an overlap: a crate
		// standing against a wardrobe is furniture, not a collision.
		const bool bApart = Max.X <= Placement.CellXZ.X || MinXZ.X >= OtherMax.X
		                 || Max.Y <= Placement.CellXZ.Y || MinXZ.Y >= OtherMax.Y;

		if (!bApart)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

void UMazeSpawnAsset::ClearAll()
{
	if (Placements.Num() == 0)
	{
		return;
	}

#if WITH_EDITOR
	Modify();
#endif

	const int32 Removed = Placements.Num();
	Placements.Reset();

	// NextId is NOT reset. The numbers already handed out may be named by a save on somebody's
	// disk; starting over would hand the same number to a different object.
	UE_LOG(LogMazeForge, Log,
		TEXT("%s: %d placements removed. Ids continue from %d."), *GetName(), Removed, NextId);

	NotifySpawnsChanged();
}

int32 UMazeSpawnAsset::AssignId(int32 Index)
{
	if (!Placements.IsValidIndex(Index))
	{
		return 0;
	}

	FMazePlacement& Placement = Placements[Index];

	// Everything that already has a number keeps it. That is the whole contract: the ids key a
	// runtime registry of what the player did to each object, and a renumbering would repoint
	// every save at the wrong one.
	if (Placement.Id == 0)
	{
#if WITH_EDITOR
		Modify();
#endif
		Placement.Id = NextId++;
	}

	return Placement.Id;
}

int32 UMazeSpawnAsset::CountUnassigned() const
{
	int32 Count = 0;

	for (const FMazePlacement& Placement : Placements)
	{
		Count += (Placement.Id == 0) ? 1 : 0;
	}

	return Count;
}

void UMazeSpawnAsset::NotifySpawnsChanged()
{
	OnSpawnsChanged.Broadcast();
	MarkPackageDirty();
}
