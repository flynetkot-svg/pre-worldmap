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

int32 UMazeSpawnAsset::FindAtCell(const UMazeObjectLibrary* InLibrary, const FIntPoint& CellXZ) const
{
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

		const FIntPoint Max = Placement.MaxCellXZ(Footprint);

		if (CellXZ.X >= Placement.CellXZ.X && CellXZ.X < Max.X
			&& CellXZ.Y >= Placement.CellXZ.Y && CellXZ.Y < Max.Y)
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
