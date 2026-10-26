#include "Spawner/MazeLadders.h"

#include "Assets/MazeObjectLibrary.h"
#include "Data/MazeGrid.h"

namespace MazeLadders
{
	bool FindShaft(const FMazeGrid& Grid, const int32 SliceY, const FIntPoint& Cell, FMazeShaft& OutShaft)
	{
		// Outside the grid and the world border count as mass: a ladder can neither stand on
		// nor climb into what will not be there.
		auto Blocked = [&Grid, SliceY](int32 X, int32 Z)
		{
			const FIntVector C(X, SliceY, Z);
			return !Grid.IsInside(C) || Grid.IsSolid(C) || Grid.IsBorderCell(C);
		};

		const int32 X = Cell.X;
		if (Blocked(X, Cell.Y))
		{
			return false;
		}

		int32 Floor = Cell.Y;
		while (!Blocked(X, Floor - 1))
		{
			--Floor;
		}

		// Mass under (Lx, Z - 1) with air under that, no lower than the floor the ladder stands
		// on: a slab hanging over the room, not a step up from its floor.
		auto IsOverhang = [&Blocked, Floor](int32 Lx, int32 Z)
		{
			if (!Blocked(Lx, Z - 1))
			{
				return false;
			}
			int32 Below = Z - 2;
			while (Below >= Floor && Blocked(Lx, Below))
			{
				--Below;
			}
			return Below >= Floor;
		};

		constexpr int32 MaxReach = 64; // how far along a row to look for somewhere to step off
		constexpr int32 MinLedge = 2;  // a one-cell lip is not a floor

		// The column above the ladder ends at the grid edge at the latest, so this stops.
		for (int32 Z = Floor + 1; !Blocked(X, Z); ++Z)
		{
			int32 Left = X;
			while (Left > X - MaxReach && !Blocked(Left - 1, Z))
			{
				--Left;
			}
			int32 Right = X;
			while (Right < X + MaxReach && !Blocked(Right + 1, Z))
			{
				++Right;
			}

			int32 Ledge = 0;
			for (int32 Lx = Left; Lx <= Right; ++Lx)
			{
				Ledge = IsOverhang(Lx, Z) ? Ledge + 1 : 0;
				if (Ledge >= MinLedge)
				{
					OutShaft.Bottom = FIntPoint(X, Floor);
					OutShaft.TopZ = Z;
					return true;
				}
			}
		}

		return false;
	}

	FName MakeLadder(const FMazeGrid& Grid, const UMazeObjectLibrary* Library, FName LadderType,
	                 const FMazeShaft& Shaft, uint32 Seed, TArray<FMazePlacement>& Out)
	{
		const FName Used = Library ? Library->PickVariant(LadderType, SpotHash(Shaft.Bottom, Seed)) : LadderType;

		FMazePlacement Ladder;
		Ladder.TypeId = Used;
		Ladder.CellXZ = Shaft.Bottom;
		Ladder.Band = EMazeDepthBand::Play;
		Ladder.Anchor = EMazeAnchorKind::Floor;
		Out.Add(Ladder);

		FMazePlacement Top;
		if (Shaft.TopZ != INDEX_NONE && MakeTopPiece(Grid, Library, Used, Shaft.Bottom.X, Shaft.TopZ, Top))
		{
			Out.Add(Top);
		}

		return Used;
	}

	bool MakeTopPiece(const FMazeGrid& Grid, const UMazeObjectLibrary* Library, FName LadderType,
	                  int32 X, int32 TopZ, FMazePlacement& Out)
	{
		const FMazeObjectType* Ladder = Library ? Library->FindType(LadderType) : nullptr;
		if (!Ladder || Ladder->TopPieceType.IsNone())
		{
			return false;
		}

		const FMazeObjectType* Top = Library->FindType(Ladder->TopPieceType);
		const int32 Height = Top ? FMath::Max(1, Top->FootprintCells.Y) : 1;

		Out = FMazePlacement();
		Out.TypeId = Ladder->TopPieceType;
		Out.CellXZ = FIntPoint(X, TopZ);
		Out.Band = EMazeDepthBand::Play;

		// Free, because what is under it is the hole. A Free object stands in the middle of its
		// box, so half the box down puts its pivot on the upper floor's level.
		Out.Anchor = EMazeAnchorKind::Free;
		Out.Offset = FVector(0.0, 0.0, -0.5 * Height * Grid.CellSize.Z);
		return true;
	}

	uint32 SpotHash(const FIntPoint& Cell, uint32 Seed)
	{
		return HashCombine(GetTypeHash(Cell), Seed);
	}
}
