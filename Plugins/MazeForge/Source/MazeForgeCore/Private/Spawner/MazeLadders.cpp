#include "Spawner/MazeLadders.h"

#include "Assets/MazeObjectLibrary.h"
#include "Data/MazeGrid.h"

namespace MazeLadders
{
	bool FindColumn(const FMazeGrid& Grid, const int32 SliceY, const FIntPoint& Cell, FMazeShaft& OutShaft)
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
		// on: a slab or the floor of a side corridor, not a step up from the floor below.
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

		// In centimetres, so the rules mean the same on a 10 cm grid and a 100 cm one.
		const double CellX = FMath::Max(1.0, Grid.CellSize.X);
		const double CellZ = FMath::Max(1.0, Grid.CellSize.Z);
		const int32 Reach = FMath::Max(2, FMath::CeilToInt(200.0 / CellX));     // a step off a ladder
		const int32 MinLedge = FMath::Max(2, FMath::CeilToInt(120.0 / CellX));  // a floor, not a bump
		const int32 Headroom = FMath::Max(2, FMath::CeilToInt(150.0 / CellZ));  // room to stand up
		const int32 MaxHole = FMath::Max(2, FMath::CeilToInt(400.0 / CellX));   // a hole, not a room

		auto IsLedge = [&Blocked, &IsOverhang, MinLedge, Headroom](int32 FromX, int32 Dir, int32 Z)
		{
			for (int32 K = 0; K < MinLedge; ++K)
			{
				const int32 Lx = FromX + Dir * K;
				if (Blocked(Lx, Z) || !IsOverhang(Lx, Z))
				{
					return false;
				}
			}
			for (int32 Up = 1; Up < Headroom; ++Up)
			{
				if (Blocked(FromX, Z + Up))
				{
					return false;
				}
			}
			return true;
		};

		OutShaft = FMazeShaft();
		OutShaft.Bottom = FIntPoint(X, Floor);

		// The column above the floor ends at the grid edge at the latest, so this stops.
		int32 Z = Floor + 1;
		for (; !Blocked(X, Z); ++Z)
		{
			// Somewhere to step off, right beside the ladder: outwards over open air, the first
			// cell with mass under it. Scanning the whole row found a "floor" on every lump of
			// erosion in the room.
			bool bLedge = false;
			for (const int32 Dir : { -1, 1 })
			{
				for (int32 Step = 1; Step <= Reach && !bLedge; ++Step)
				{
					const int32 Lx = X + Dir * Step;
					if (Blocked(Lx, Z))
					{
						break; // a wall beside the ladder
					}
					if (!Blocked(Lx, Z - 1))
					{
						continue; // still over the hole
					}
					bLedge = IsLedge(Lx, Dir, Z);
					break;
				}
			}
			if (!bLedge)
			{
				continue;
			}

			// And the column is in a gap there — a hole or a shaft, bounded on both sides — not
			// open air beside a pillar standing in a room.
			int32 HoleMin = X;
			while (X - HoleMin < MaxHole && !Blocked(HoleMin - 1, Z - 1))
			{
				--HoleMin;
			}
			int32 HoleMax = X;
			while (HoleMax - X < MaxHole && !Blocked(HoleMax + 1, Z - 1))
			{
				++HoleMax;
			}
			if (!Blocked(HoleMin - 1, Z - 1) || !Blocked(HoleMax + 1, Z - 1)
				|| HoleMax - HoleMin + 1 > MaxHole)
			{
				continue;
			}

			// One exit per floor: a floor a step higher is the same floor, eroded.
			if (OutShaft.Exits.Num() > 0 && Z - OutShaft.Exits.Last() < Headroom)
			{
				continue;
			}

			if (OutShaft.Exits.Num() == 0)
			{
				OutShaft.HoleMinX = HoleMin;
				OutShaft.HoleMaxX = HoleMax;
			}
			OutShaft.Exits.Add(Z);
		}

		OutShaft.CeilingZ = Z;
		OutShaft.TopZ = OutShaft.Exits.Num() > 0 ? OutShaft.Exits[0] : INDEX_NONE;
		return true;
	}

	bool FindShaft(const FMazeGrid& Grid, int32 SliceY, const FIntPoint& Cell, FMazeShaft& OutShaft)
	{
		return FindColumn(Grid, SliceY, Cell, OutShaft) && OutShaft.Exits.Num() > 0;
	}

	int32 LevelAtOrBelow(const FMazeShaft& Shaft, int32 Z)
	{
		int32 Level = Shaft.Bottom.Y;
		for (const int32 Exit : Shaft.Exits)
		{
			if (Exit <= Z)
			{
				Level = Exit;
			}
		}
		return Level;
	}

	int32 SnapTop(const FMazeShaft& Shaft, int32 BottomZ, int32 Z, int32 Tolerance)
	{
		// The nearest exit first, above or below the cursor: aiming a pixel under a level must
		// still land on it, or the next ladder has nothing to stand on.
		int32 Top = INDEX_NONE;
		int32 Best = Tolerance + 1;
		for (const int32 Exit : Shaft.Exits)
		{
			if (Exit > BottomZ && FMath::Abs(Exit - Z) < Best)
			{
				Best = FMath::Abs(Exit - Z);
				Top = Exit;
			}
		}
		if (Top != INDEX_NONE)
		{
			return Top;
		}

		for (const int32 Exit : Shaft.Exits)
		{
			if (Exit > BottomZ && Exit <= Z)
			{
				Top = Exit;
			}
		}
		if (Top == INDEX_NONE)
		{
			// No exit there: the designer's word goes, as long as the ladder stays in the column.
			Top = FMath::Min(Z, Shaft.CeilingZ);
		}
		return Top > BottomZ ? Top : INDEX_NONE;
	}

	bool FindStackBase(const TArray<FMazePlacement>& Placements, const UMazeObjectLibrary* Library,
	                   const FIntPoint& Cell, int32 Tolerance, FIntPoint& OutBase)
	{
		int32 Best = Tolerance + 1;
		for (const FMazePlacement& Piece : Placements)
		{
			const FMazeObjectType* Type = Library ? Library->FindType(Piece.TypeId) : nullptr;
			if (!Type)
			{
				continue;
			}

			const int32 Width = FMath::Max(1, Type->FootprintCells.X);
			if (Cell.X < Piece.CellXZ.X || Cell.X >= Piece.CellXZ.X + Width)
			{
				continue; // not this column
			}

			int32 Level = INDEX_NONE;
			if (Library->IsTopPiece(Piece.TypeId))
			{
				Level = Piece.CellXZ.Y;                     // a platform: its row is the level
			}
			else if (Type->Category == EMazeObjectCategory::Climb && Type->LiftRole == EMazeLiftRole::None
				&& !Type->bRope && Piece.ClimbCells > 0)
			{
				Level = Piece.CellXZ.Y + Piece.ClimbCells;  // a sized ladder: where it ends
			}

			if (Level != INDEX_NONE && FMath::Abs(Level - Cell.Y) < Best)
			{
				Best = FMath::Abs(Level - Cell.Y);
				OutBase = FIntPoint(Piece.CellXZ.X, Level);
			}
		}
		return Best <= Tolerance;
	}

	void FindAllShafts(const FMazeGrid& Grid, int32 SliceY, TArray<FMazeShaft>& Out)
	{
		// Every cell a ladder could stand in is the empty cell on top of some mass, so the mass
		// is what is walked: the grid stores mass only, and the air is everything else.
		TMap<FIntPoint, FMazeShaft> ByHole; // (hole's left cell, first exit row) -> best shaft

		// The tops of the mass, plus the lowest free cell of every column: the bottom of the
		// map may be the world border, which is built but never stored as cells.
		TArray<FIntPoint> Spots;
		for (const TPair<FIntVector, FMazeCell>& Pair : Grid.Cells)
		{
			if (Pair.Key.Y == SliceY && Pair.Value.IsSolid())
			{
				Spots.Add(FIntPoint(Pair.Key.X, Pair.Key.Z + 1));
			}
		}
		for (int32 X = 0; X < Grid.SizeXZ.X; ++X)
		{
			for (int32 Z = 0; Z < FMath::Min(64, Grid.SizeXZ.Y); ++Z)
			{
				const FIntVector C(X, SliceY, Z);
				if (!Grid.IsSolid(C) && !Grid.IsBorderCell(C))
				{
					if (Z > 0)
					{
						Spots.Add(FIntPoint(X, Z));
					}
					break;
				}
			}
		}

		for (const FIntPoint& Spot : Spots)
		{
			FMazeShaft Shaft;
			if (!FindShaft(Grid, SliceY, Spot, Shaft) || Shaft.Bottom.Y != Spot.Y)
			{
				continue;
			}

			const FIntPoint Key(Shaft.HoleMinX, Shaft.TopZ);
			const int32 Middle2 = Shaft.HoleMinX + Shaft.HoleMaxX; // twice the middle, no rounding
			auto Off = [Middle2](const FMazeShaft& S)
			{
				return FMath::Abs(2 * S.Bottom.X - Middle2) * 2 + (2 * S.Bottom.X < Middle2 ? 0 : 1);
			};

			FMazeShaft* Best = ByHole.Find(Key);
			if (!Best)
			{
				ByHole.Add(Key, Shaft);
			}
			else if (Off(Shaft) < Off(*Best))
			{
				*Best = Shaft;
			}
		}

		Out.Reset(ByHole.Num());
		for (const TPair<FIntPoint, FMazeShaft>& Pair : ByHole)
		{
			Out.Add(Pair.Value);
		}

		// A stable order, so the variants and the undo history do not depend on the hash map.
		Out.Sort([](const FMazeShaft& A, const FMazeShaft& B)
		{
			return A.TopZ != B.TopZ ? A.TopZ < B.TopZ : A.Bottom.X < B.Bottom.X;
		});
	}

	bool IsLadderPiece(const UMazeObjectLibrary* Library, const FMazePlacement& Placement)
	{
		const FMazeObjectType* Type = Library ? Library->FindType(Placement.TypeId) : nullptr;
		// A lift may well be filed under Climb; it is not a ladder all the same.
		return Type && Type->LiftRole == EMazeLiftRole::None && !Type->bRope
			&& (Type->Category == EMazeObjectCategory::Climb || Library->IsTopPiece(Placement.TypeId));
	}

	void FindPieces(const TArray<FMazePlacement>& Placements, const UMazeObjectLibrary* Library,
	                int32 MinX, int32 MaxX, int32 MinZ, int32 MaxZ, TArray<int32>& OutIndices)
	{
		for (int32 Index = 0; Index < Placements.Num(); ++Index)
		{
			const FMazePlacement& Piece = Placements[Index];
			if (!IsLadderPiece(Library, Piece))
			{
				continue;
			}

			const FMazeObjectType* Type = Library->FindType(Piece.TypeId);
			const FIntPoint Max = Piece.MaxCellXZ(Type->FootprintCells); // exclusive
			const int32 MaxZPiece = FMath::Max(Max.Y, Piece.CellXZ.Y + Piece.ClimbCells);
			if (Piece.CellXZ.X <= MaxX && MinX < Max.X && Piece.CellXZ.Y <= MaxZ && MinZ < MaxZPiece)
			{
				OutIndices.Add(Index);
			}
		}
	}

	FName MakeLadder(const FMazeGrid& Grid, const UMazeObjectLibrary* Library, FName LadderType,
	                 int32 X, int32 BottomZ, int32 TopZ, bool bOnFloor, uint32 Seed,
	                 TArray<FMazePlacement>& Out)
	{
		const FIntPoint Base(X, BottomZ);
		const FName Used = Library ? Library->PickVariant(LadderType, SpotHash(Base, Seed)) : LadderType;

		FMazePlacement Ladder;
		Ladder.TypeId = Used;
		Ladder.CellXZ = Base;
		Ladder.Band = EMazeDepthBand::Play;
		Ladder.ClimbCells = FMath::Max(1, TopZ - BottomZ);

		if (bOnFloor)
		{
			Ladder.Anchor = EMazeAnchorKind::Floor;
		}
		else
		{
			// Standing on the platform of the ladder below, over its hole: nothing to anchor to.
			// A Free object stands in the middle of its box; half the box down puts its pivot on
			// the level.
			const FMazeObjectType* Type = Library ? Library->FindType(Used) : nullptr;
			const int32 Height = Type ? FMath::Max(1, Type->FootprintCells.Y) : 1;
			Ladder.Anchor = EMazeAnchorKind::Free;
			Ladder.Offset = FVector(0.0, 0.0, -0.5 * Height * Grid.CellSize.Z);
		}
		Out.Add(Ladder);

		FMazePlacement Top;
		if (MakeTopPiece(Grid, Library, Used, X, TopZ, Top))
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

	double ClimbValue(const FMazeObjectType& Type, double HeightCm)
	{
		const double Units = Type.ClimbUnitCm > 0.0f
			? FMath::RoundToDouble(HeightCm / Type.ClimbUnitCm)
			: HeightCm;
		return FMath::Max(1.0, Units + Type.ClimbExtraUnits);
	}

	uint32 SpotHash(const FIntPoint& Cell, uint32 Seed)
	{
		return HashCombine(GetTypeHash(Cell), Seed);
	}
}
