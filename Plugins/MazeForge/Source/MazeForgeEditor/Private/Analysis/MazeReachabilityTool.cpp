#include "Analysis/MazeReachabilityTool.h"

#include "Analysis/MazePathGraph.h"
#include "Analysis/MazeReachability.h"
#include "Assets/MazeGridAsset.h"
#include "Assets/MazeObjectLibrary.h"
#include "Assets/MazeSpawnAsset.h"
#include "Data/MazeRoomDesc.h"
#include "Editor.h"
#include "Generators/MazeGenerator_Classic.h"
#include "Misc/ScopedSlowTask.h"
#include "Slicers/MazeRoomSlicerBase.h"
#include "Spawner/MazeLadders.h"
#include "MazeForgeCore.h"
#include "Mode/MazeEdModeSettings.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MazeReachability"

namespace
{
	struct FInputs
	{
		FMazeReachParams Params;
		int32 EntryCount = 0;

		/** Gate points: where the main path is meant to end. */
		TArray<FIntPoint> Exits;

		/** Placement index behind each entry of Params.Doors and Params.Keys. */
		TArray<int32> DoorPlacements;
		TArray<int32> KeyPlacements;
	};

	/** Starts and ladders from the spawn asset, heights from the generator and the cell size. */
	FInputs GatherInputs(const UMazeEdModeSettings& Settings, const UMazeGridAsset& Asset)
	{
		FInputs In;
		In.Params.AgentHeightCells = Asset.Generator ? FMath::Max(1, Asset.Generator->AgentHeightCells) : 2;

		const double CellZ = FMath::Max(1.0, Asset.Grid.CellSize.Z);
		const double CellX = FMath::Max(1.0, Asset.Grid.CellSize.X);
		In.Params.bGravity = Settings.bRequireLadders;
		In.Params.ClimbUpCells = FMath::FloorToInt(FMath::Max(Settings.StepUpCm, Settings.JumpHeightCm) / CellZ);
		In.Params.AgentWidthCells = FMath::Max(1, FMath::CeilToInt(Settings.AgentWidthCm / CellX));

		const UMazeSpawnAsset* Spawns = Asset.Spawns.LoadSynchronous();
		const UMazeObjectLibrary* Library = Spawns ? Spawns->Library.LoadSynchronous() : nullptr;
		if (!Spawns || !Library)
		{
			return In;
		}

		for (int32 Index = 0; Index < Spawns->Placements.Num(); ++Index)
		{
			const FMazePlacement& Placement = Spawns->Placements[Index];
			const FMazeObjectType* Type = Library->FindType(Placement.TypeId);
			if (!Type)
			{
				continue;
			}

			const FIntRect Rect(Placement.CellXZ, Placement.MaxCellXZ(Type->FootprintCells));
			if (Type->LockRole == EMazeLockRole::Door)
			{
				In.Params.Doors.Add({ Rect, Placement.LockId });
				In.DoorPlacements.Add(Index);
			}
			else if (Type->LockRole == EMazeLockRole::Key)
			{
				In.Params.Keys.Add({ Rect, Placement.LockId });
				In.KeyPlacements.Add(Index);
			}

			// A lift carries him its whole travel, as a ladder does its whole height.
			// A rope is run across: its cells hold him up, as floor would.
			if (Type->bRope && Placement.SpanCells > 0)
			{
				In.Params.Walkways.Add(FIntRect(Placement.CellXZ,
					Placement.CellXZ + FIntPoint(Placement.SpanCells, 1)));
			}
			else if (Type->Category == EMazeObjectCategory::Climb || Type->LiftRole == EMazeLiftRole::Platform)
			{
				// A ladder sized to its shaft climbs its Climb Cells, not just its footprint.
				FIntPoint Max = Placement.MaxCellXZ(Type->FootprintCells);
				Max.Y = FMath::Max(Max.Y, Placement.CellXZ.Y + Placement.ClimbCells);
				In.Params.Climbables.Add(FIntRect(Placement.CellXZ, Max));
			}

			if (Type->TransitionRole == EMazeTransitionRole::Entry)
			{
				In.Params.Starts.Add(Placement.CellXZ);
				++In.EntryCount;
			}
			else if (Type->TransitionRole == EMazeTransitionRole::Gate)
			{
				In.Exits.Add(Placement.CellXZ);
			}
		}

		return In;
	}

	/** The built slicing, or the one the slicer would make right now: rooms exist before Apply too. */
	TArray<FMazeRoomDesc> GatherRooms(const UMazeGridAsset& Asset)
	{
		if (Asset.Rooms.Num() > 0)
		{
			return Asset.Rooms;
		}

		TArray<FMazeRoomDesc> Rooms;
		if (Asset.Slicer)
		{
			Asset.Slicer->Execute(Asset.Grid, Rooms);
		}
		return Rooms;
	}

	/** Steps from one room to every other through their links. INDEX_NONE — no way at all. */
	TArray<int32> RoomDistances(const TArray<FMazeRoomDesc>& Rooms, int32 From)
	{
		TArray<int32> Distance;
		Distance.Init(INDEX_NONE, Rooms.Num());
		if (!Rooms.IsValidIndex(From))
		{
			return Distance;
		}

		TMap<FName, int32> IndexOf;
		for (int32 I = 0; I < Rooms.Num(); ++I)
		{
			IndexOf.Add(Rooms[I].RoomId, I);
		}

		TArray<int32> Queue{ From };
		Distance[From] = 0;
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			for (const FName& Neighbor : Rooms[Queue[Head]].Neighbors)
			{
				const int32* Next = IndexOf.Find(Neighbor);
				if (Next && Distance[*Next] == INDEX_NONE)
				{
					Distance[*Next] = Distance[Queue[Head]] + 1;
					Queue.Add(*Next);
				}
			}
		}
		return Distance;
	}

	/** The room under a cell, or INDEX_NONE. */
	int32 RoomIndexAt(const TArray<FMazeRoomDesc>& Rooms, const FIntPoint& Cell)
	{
		return MazeRooms::IndexAtXZ(Rooms, Cell.X, Cell.Y);
	}

	FString DescribeAt(const TCHAR* What, FName LockId, const FIntPoint& Cell)
	{
		return FString::Printf(TEXT("%s '%s' at (%d, %d)"), What,
			LockId.IsNone() ? TEXT("no Lock Id") : *LockId.ToString(), Cell.X, Cell.Y);
	}

	/** The ladder to stand in a climbed shaft, and how tall one piece of it is. */
	FName ResolveLadder(const UMazeEdModeSettings& Settings, const UMazeGridAsset& Asset,
	                    FIntPoint& OutFootprint)
	{
		FName Ladder = Settings.FixLadderType;
		if (Ladder.IsNone())
		{
			if (const UMazeGenerator_Classic* Classic = Cast<UMazeGenerator_Classic>(Asset.Generator))
			{
				Ladder = Classic->LadderType;
			}
		}

		const UMazeSpawnAsset* Spawns = Asset.Spawns.LoadSynchronous();
		const UMazeObjectLibrary* Library = Spawns ? Spawns->Library.LoadSynchronous() : nullptr;
		const FMazeObjectType* Type = (Library && !Ladder.IsNone()) ? Library->FindType(Ladder) : nullptr;
		if (!Type)
		{
			return NAME_None;
		}

		OutFootprint = FIntPoint(FMath::Max(1, Type->FootprintCells.X), FMath::Max(1, Type->FootprintCells.Y));
		return Ladder;
	}
}

void MazeReachabilityTool::Check(UMazeEdModeSettings& Settings)
{
	Settings.ClearReachability();

	const UMazeGridAsset* Asset = Settings.TargetAsset.LoadSynchronous();
	if (!Asset)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Reachability: no Target Asset set."));
		return;
	}

	const FInputs In = GatherInputs(Settings, *Asset);

	FMazeReachResult Result;
	MazeReachability::Solve(Asset->Grid, In.Params, Result);

	TArray<FMazeReachIsland> Islands;
	MazeReachability::FindIslands(Result, Islands);

	const TArray<FMazeRoomDesc> Rooms = GatherRooms(*Asset);

	int32 Fine = 0;
	int32 Unreachable = 0;
	int32 Partial = 0;
	int32 BehindDoors = 0;
	TArray<FString> UnreachableNames;

	for (const FMazeRoomDesc& Room : Rooms)
	{
		int32 Standable = 0;
		int32 Reached = 0;
		int32 ReachedFree = 0;
		for (int32 Z = Room.MinXZ.Y; Z < Room.MaxXZ.Y; ++Z)
		{
			for (int32 X = Room.MinXZ.X; X < Room.MaxXZ.X; ++X)
			{
				if (Result.IsStandable(X, Z))
				{
					++Standable;
					Reached += Result.IsReached(X, Z) ? 1 : 0;
					ReachedFree += Result.ReachedWithoutKeys[Result.Index(X, Z)] ? 1 : 0;
				}
			}
		}

		uint8 Status = 0;
		if (Standable == 0)
		{
			++Fine;
		}
		else if (Reached == 0)
		{
			Status = 1;
			++Unreachable;
			UnreachableNames.Add(Room.RoomId.ToString());
		}
		else if (Reached < Standable)
		{
			Status = 2;
			++Partial;
		}
		else if (ReachedFree < Reached)
		{
			Status = 3; // all of it can be had, some of it only through a door
			++BehindDoors;
		}
		else
		{
			++Fine;
		}

		if (Status != 0)
		{
			Settings.ReachRoomOverlay.Add({ FIntRect(Room.MinXZ, Room.MaxXZ), Status });
		}
	}

	if (Rooms.Num() == 0)
	{
		for (const FMazeReachIsland& Island : Islands)
		{
			Settings.ReachIslandBounds.Add(FIntRect(Island.Min, Island.Max + FIntPoint(1, 1)));
		}
	}

	// ------------------------------------------------------------- doors and keys
	TArray<FString> Problems;
	auto Report = [&Settings, &Problems](const FIntRect& Rect, const FString& Text)
	{
		Settings.ReachProblemRects.Add(Rect);
		Problems.Add(Text);
	};

	for (int32 I = 0; I < In.Params.Keys.Num(); ++I)
	{
		const FMazeReachLock& Key = In.Params.Keys[I];
		if (!Result.KeyReached[I])
		{
			Report(Key.Rect, DescribeAt(TEXT("key"), Key.LockId, Key.Rect.Min)
				+ TEXT(" can never be picked up: no way leads to it, whichever doors are opened"));
		}
	}

	for (int32 D = 0; D < In.Params.Doors.Num(); ++D)
	{
		const FMazeReachLock& Door = In.Params.Doors[D];
		if (Door.LockId.IsNone())
		{
			Report(Door.Rect, DescribeAt(TEXT("door"), Door.LockId, Door.Rect.Min)
				+ TEXT(" has no Lock Id: no key will ever open it"));
			continue;
		}

		if (!Result.DoorOpened[D])
		{
			const bool bAnyKey = In.Params.Keys.ContainsByPredicate(
				[&Door](const FMazeReachLock& Key) { return Key.LockId == Door.LockId; });
			Report(Door.Rect, DescribeAt(TEXT("door"), Door.LockId, Door.Rect.Min)
				+ (bAnyKey ? TEXT(" never opens: every key to it is out of reach")
				           : TEXT(" never opens: there is no key with its Lock Id")));
		}

		// Never loaded together: a door and its key with fewer rooms between them than asked.
		const int32 DoorRoom = RoomIndexAt(Rooms, Door.Rect.Min);
		if (DoorRoom == INDEX_NONE)
		{
			continue;
		}

		const TArray<int32> Distance = RoomDistances(Rooms, DoorRoom);
		for (const FMazeReachLock& Key : In.Params.Keys)
		{
			const int32 KeyRoom = RoomIndexAt(Rooms, Key.Rect.Min);
			if (Key.LockId != Door.LockId || KeyRoom == INDEX_NONE || Distance[KeyRoom] == INDEX_NONE)
			{
				continue;
			}

			const int32 Between = FMath::Max(0, Distance[KeyRoom] - 1);
			if (Distance[KeyRoom] == 0 || Between < Settings.MinRoomsBetweenKeyAndDoor)
			{
				Report(Key.Rect, DescribeAt(TEXT("key"), Key.LockId, Key.Rect.Min)
					+ FString::Printf(TEXT(" is %s its door at (%d, %d); at least %d room(s) must lie between"),
						Distance[KeyRoom] == 0 ? TEXT("in the same room as")
						                       : *FString::Printf(TEXT("%d room(s) from"), Between),
						Door.Rect.Min.X, Door.Rect.Min.Y, Settings.MinRoomsBetweenKeyAndDoor));
			}
			else if (Settings.MaxRoomsBetweenKeyAndDoor > 0 && Between > Settings.MaxRoomsBetweenKeyAndDoor)
			{
				Report(Key.Rect, DescribeAt(TEXT("key"), Key.LockId, Key.Rect.Min)
					+ FString::Printf(TEXT(" is %d rooms from its door at (%d, %d); at most %d may lie between"),
						Between, Door.Rect.Min.X, Door.Rect.Min.Y, Settings.MaxRoomsBetweenKeyAndDoor));
			}
		}
	}

	// ------------------------------------------------------------- the limits
	TMap<int32, TArray<int32>> KeysByRound; // round of doors -> keys that come into reach in it
	for (int32 I = 0; I < In.Params.Keys.Num(); ++I)
	{
		const int32 Round = Result.KeyPass[I];
		if (Round == INDEX_NONE)
		{
			continue;
		}

		KeysByRound.FindOrAdd(Round).Add(I);
		if (Round + 1 > Settings.MaxLockNesting)
		{
			const FMazeReachLock& Key = In.Params.Keys[I];
			Report(Key.Rect, DescribeAt(TEXT("key"), Key.LockId, Key.Rect.Min)
				+ FString::Printf(TEXT(" lies behind %d door(s) in a row; Max Lock Nesting allows %d"),
					Round, Settings.MaxLockNesting - 1));
		}
	}

	for (const TPair<int32, TArray<int32>>& Round : KeysByRound)
	{
		TSet<FName> Colours;
		for (const int32 I : Round.Value)
		{
			Colours.Add(In.Params.Keys[I].LockId);
		}
		if (Colours.Num() > Settings.MaxKeysHeld)
		{
			const FMazeReachLock& Key = In.Params.Keys[Round.Value[0]];
			Report(Key.Rect, FString::Printf(TEXT("%d keys come into reach together after %d door round(s), "
				"before any of their doors; Max Keys Held allows %d"), Colours.Num(), Round.Key, Settings.MaxKeysHeld));
		}
	}

	// How much of the maze each colour shuts away: the play-through again without its keys.
	if (Result.NumStandable > 0)
	{
		TSet<FName> Checked;
		for (const FMazeReachLock& Door : In.Params.Doors)
		{
			if (Door.LockId.IsNone() || Checked.Contains(Door.LockId))
			{
				continue;
			}
			Checked.Add(Door.LockId);

			FMazeReachParams Without = In.Params;
			Without.Keys.RemoveAll([&Door](const FMazeReachLock& Key) { return Key.LockId == Door.LockId; });
			FMazeReachResult Shut;
			MazeReachability::Solve(Asset->Grid, Without, Shut);

			const float Share = float(Result.NumReachedStandable - Shut.NumReachedStandable) / Result.NumStandable;
			if (Share > Settings.MaxLockedShare)
			{
				Report(Door.Rect, DescribeAt(TEXT("door"), Door.LockId, Door.Rect.Min)
					+ FString::Printf(TEXT(" and the doors of its colour close off %.0f%% of the maze; Max Locked Share is %.0f%%"),
						Share * 100.0f, Settings.MaxLockedShare * 100.0f));
			}
		}
	}

	for (const FString& Problem : Problems)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Locks: %s."), *Problem);
	}

	Settings.ReachAsset = Asset;

	const FString From = In.EntryCount > 0
		? FString::Printf(TEXT("from %d entry point(s)"), In.EntryCount)
		: (Result.UsedStarts.Num() > 0
			? FString::Printf(TEXT("from the lowest place to stand (%d, %d) — no Entry point placed"),
				Result.UsedStarts[0].X, Result.UsedStarts[0].Y)
			: FString(TEXT("from nowhere — there is no place to stand")));

	const FString LockLine = (In.Params.Doors.Num() + In.Params.Keys.Num()) > 0
		? FString::Printf(TEXT("; %d door(s), %d key(s), %d room(s) behind doors, %d lock problem(s)"),
			In.Params.Doors.Num(), In.Params.Keys.Num(), BehindDoors, Problems.Num())
		: FString();

	Settings.ReachabilityStatus = (Islands.Num() == 0
		? FString::Printf(TEXT("all reachable (%d places to stand)"), Result.NumStandable)
		: FString::Printf(TEXT("%d unreachable piece(s); rooms: %d fine, %d unreachable, %d partly"),
			Islands.Num(), Fine, Unreachable, Partial)) + LockLine;

	UE_LOG(LogMazeForge, Log,
		TEXT("Reachability %s, player %d x %d cells, %s, %d climbable(s): "
		     "%d of %d places to stand reached, %d unreachable piece(s). Rooms: %d fine, %d unreachable, "
		     "%d reached in part.%s%s"),
		*From, In.Params.AgentWidthCells, In.Params.AgentHeightCells,
		In.Params.bGravity
			? *FString::Printf(TEXT("gravity on, up %d cell(s) without a ladder"), In.Params.ClimbUpCells)
			: TEXT("no gravity (any shaft counts as a way up)"),
		In.Params.Climbables.Num(),
		Result.NumReachedStandable, Result.NumStandable, Islands.Num(), Fine, Unreachable, Partial,
		UnreachableNames.Num() > 0 ? *(TEXT(" Unreachable: ") + FString::Join(UnreachableNames, TEXT(", "))) : TEXT(""),
		*LockLine);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

void MazeReachabilityTool::Fix(UMazeEdModeSettings& Settings)
{
	UMazeGridAsset* Asset = Settings.TargetAsset.LoadSynchronous();
	if (!Asset)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Fix reachability: no Target Asset set."));
		return;
	}

	UMazeSpawnAsset* Spawns = Asset->Spawns.LoadSynchronous();
	FIntPoint LadderFootprint(1, 1);

	// Without gravity a shaft is a way up by itself: nothing is stood in it.
	const FName Ladder = (Spawns && Settings.bRequireLadders)
		? ResolveLadder(Settings, *Asset, LadderFootprint) : NAME_None;
	const bool bAllowUp = !Settings.bRequireLadders || !Ladder.IsNone();

	FInputs In = GatherInputs(Settings, *Asset);

	// Geometry only. A piece behind a door that never opens is a lock problem, not a missing
	// passage — carving round the door would quietly undo the design. Every door counts as open.
	In.Params.Doors.Reset();
	In.Params.Keys.Reset();

	const FScopedTransaction Transaction(LOCTEXT("FixReachability", "MazeForge: Fix Reachability"));
	Asset->Modify();

	TArray<FMazePlacement> NewLadders;
	TSet<int32> Skipped; // standing cells of pieces that could not be joined
	int32 Joined = 0;
	int32 Carved = 0;
	int32 LeftUp = 0;
	int32 LeftOther = 0;

	constexpr int32 MaxPieces = 64;
	for (int32 Pass = 0; Pass < MaxPieces; ++Pass)
	{
		FMazeReachResult Result;
		MazeReachability::Solve(Asset->Grid, In.Params, Result);

		TArray<FMazeReachIsland> Islands;
		MazeReachability::FindIslands(Result, Islands);

		bool bJoinedOne = false;
		LeftUp = 0;
		LeftOther = 0;

		for (const FMazeReachIsland& Island : Islands)
		{
			if (Skipped.Contains(Island.Cells[0].Y * Result.Size.X + Island.Cells[0].X))
			{
				continue;
			}

			MazeReachability::FConnection Connection;
			const bool bOk = MazeReachability::ConnectIsland(Asset->Grid, Result, Island,
				In.Params.AgentHeightCells, In.Params.AgentWidthCells, Settings.FixShaftWidthCells,
				bAllowUp, Connection);

			if (!bOk)
			{
				for (const FIntPoint& Cell : Island.Cells)
				{
					Skipped.Add(Cell.Y * Result.Size.X + Cell.X);
				}
				(bAllowUp ? LeftOther : LeftUp) += 1;
				continue;
			}

			Carved += Connection.CarvedCells;
			if (Connection.bNeedsLadder && !Ladder.IsNone())
			{
				const UMazeObjectLibrary* Library = Spawns->Library.LoadSynchronous();
				const FName Used = Library
					? Library->PickVariant(Ladder, MazeLadders::SpotHash(Connection.LadderBase, 0))
					: Ladder;

				// Stacked pieces of the one ladder type, as many as the climb is tall — or a
				// single one, when the ladder sizes itself to the hole at export. Free anchor
				// above the first: only the lowest has a floor under it.
				const FMazeObjectType* UsedType = Library ? Library->FindType(Used) : nullptr;
				const int32 PieceStep = (UsedType && !UsedType->ClimbHeightProperty.IsNone())
					? FMath::Max(1, Connection.LadderSpan) : LadderFootprint.Y;

				for (int32 Z = 0; Z < Connection.LadderSpan; Z += PieceStep)
				{
					FMazePlacement Piece;
					Piece.TypeId = Used;
					Piece.CellXZ = FIntPoint(Connection.LadderBase.X, Connection.LadderBase.Y + Z);
					Piece.Band = EMazeDepthBand::Play;
					Piece.Anchor = Z == 0 ? EMazeAnchorKind::Floor : EMazeAnchorKind::Free;
					Piece.ClimbCells = PieceStep == Connection.LadderSpan ? Connection.LadderSpan : 0;
					Piece.bStructural = true;
					NewLadders.Add(Piece);
					In.Params.Climbables.Add(FIntRect(Piece.CellXZ, Piece.CellXZ
						+ FIntPoint(LadderFootprint.X, FMath::Max(LadderFootprint.Y, Piece.ClimbCells))));
				}

				// And the platform where the climb ends, if the ladder has one.
				FMazePlacement Top;
				if (MazeLadders::MakeTopPiece(Asset->Grid, Library, Used, Connection.LadderBase.X,
					Connection.LadderBase.Y + Connection.LadderSpan, Top))
				{
					Top.bStructural = true;
					NewLadders.Add(Top);
				}
			}

			++Joined;
			bJoinedOne = true;
			break; // the maze changed: look again before joining the next piece
		}

		if (!bJoinedOne)
		{
			break;
		}
	}

	if (NewLadders.Num() > 0 && Spawns)
	{
		Spawns->AddBatch(NewLadders);
	}

	if (Joined > 0)
	{
		Asset->NotifyGridChanged();
	}

	UE_LOG(LogMazeForge, Log,
		TEXT("Fix reachability: %d piece(s) joined, %d cell(s) carved, %d ladder piece(s) placed%s. "
		     "Ctrl+Z undoes it all; Apply Changes builds it."),
		Joined, Carved, NewLadders.Num(),
		LeftUp + LeftOther > 0
			? *FString::Printf(TEXT("; %d piece(s) left%s"), LeftUp + LeftOther,
				LeftUp > 0 ? TEXT(" — reaching some means climbing, and there is no ladder type: set "
				                  "Fix Ladder Type, or Ladder Type on the Classic generator")
				           : TEXT(""))
			: TEXT(""));

	Check(Settings);
}

namespace
{
	/** The library type for a lock role: the named one, or the first with that role. */
	const FMazeObjectType* ResolveLockType(const UMazeObjectLibrary& Library, FName Wanted, EMazeLockRole Role)
	{
		if (!Wanted.IsNone())
		{
			return Library.FindType(Wanted);
		}
		return Library.Types.FindByPredicate([Role](const FMazeObjectType& Type) { return Type.LockRole == Role; });
	}

	/**
	 *  Whether a footprint stands on a floor in open air at this cell, in the world of a result.
	 *  MaxSeal > 0: the column must also be no taller than that — a door seals a passage, not a hall.
	 */
	bool StandsHere(const FMazeReachResult& World, const FIntPoint& Cell, const FIntPoint& Footprint, int32 MaxSeal)
	{
		for (int32 Dx = 0; Dx < Footprint.X; ++Dx)
		{
			const int32 X = Cell.X + Dx;
			if (!World.IsInside(X, Cell.Y) || !World.IsInside(X, Cell.Y - 1))
			{
				return false;
			}

			const int32 Free = World.FreeAbove[World.Index(X, Cell.Y)];
			if (Free < Footprint.Y || (MaxSeal > 0 && Free > MaxSeal) || !World.Solid[World.Index(X, Cell.Y - 1)])
			{
				return false;
			}
		}
		return true;
	}
}

void MazeReachabilityTool::GenerateLocks(UMazeEdModeSettings& Settings)
{
	UMazeGridAsset* Asset = Settings.TargetAsset.LoadSynchronous();
	UMazeSpawnAsset* Spawns = Asset ? Asset->Spawns.LoadSynchronous() : nullptr;
	const UMazeObjectLibrary* Library = Spawns ? Spawns->Library.LoadSynchronous() : nullptr;
	if (!Library)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Generate locks: the target needs a Spawns asset with an object library."));
		return;
	}

	const FMazeObjectType* DoorType = ResolveLockType(*Library, Settings.LockDoorType, EMazeLockRole::Door);
	const FMazeObjectType* KeyType = ResolveLockType(*Library, Settings.LockKeyType, EMazeLockRole::Key);
	if (!DoorType || !KeyType)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Generate locks: %s %s not found. Give a library type Lock Role Door and another "
			     "Lock Role Key, or name them in Lock Door Type and Lock Key Type."),
			!DoorType ? TEXT("the door type") : TEXT(""), !KeyType ? TEXT("the key type") : TEXT(""));
		return;
	}

	TArray<FName> Ids = Settings.LockIds;
	Ids.RemoveAll([](const FName& Id) { return Id.IsNone(); });
	if (Ids.Num() == 0)
	{
		Ids.Add(TEXT("Red"));
	}

	const FScopedTransaction Transaction(LOCTEXT("GenerateLocks", "MazeForge: Generate Locks"));
	const int32 Removed = Spawns->RemoveLockGenerated();

	// What stands already — hand-placed doors and keys included — is part of the puzzle.
	FInputs In = GatherInputs(Settings, *Asset);
	const TArray<FMazeRoomDesc> Rooms = GatherRooms(*Asset);

	const FIntPoint DoorFootprint(FMath::Max(1, DoorType->FootprintCells.X), FMath::Max(1, DoorType->FootprintCells.Y));
	const FIntPoint KeyFootprint(FMath::Max(1, KeyType->FootprintCells.X), FMath::Max(1, KeyType->FootprintCells.Y));
	const int32 MaxSeal = DoorFootprint.Y + In.Params.AgentHeightCells;

	FRandomStream Rng(Settings.LockSeed);
	TArray<FMazePlacement> Placed;
	int32 Made = 0;
	FString StoppedBecause;

	FScopedSlowTask Progress(static_cast<float>(Settings.LockCount), LOCTEXT("GenerateLocksTask", "Placing doors and keys..."));
	Progress.MakeDialog();

	for (int32 Lock = 0; Lock < Settings.LockCount; ++Lock)
	{
		Progress.EnterProgressFrame(1.0f);
		const FName LockId = Ids[Lock % Ids.Num()];

		FMazeReachResult Base;
		MazeReachability::Solve(Asset->Grid, In.Params, Base);

		// Where a door could stand: on a floor the player gets to, in a passage it fills.
		TArray<FIntPoint> DoorSpots;
		for (int32 Z = 1; Z < Base.Size.Y; ++Z)
		{
			for (int32 X = 0; X < Base.Size.X; ++X)
			{
				const FIntPoint Cell(X, Z);
				if (!Base.IsReached(X, Z) || !Base.IsStandable(X, Z) || !StandsHere(Base, Cell, DoorFootprint, MaxSeal))
				{
					continue;
				}

				const FIntRect DoorRect(Cell, Cell + DoorFootprint);
				const bool bOnStart = Base.UsedStarts.ContainsByPredicate([&DoorRect, &In](const FIntPoint& Start)
				{
					return Start.X + In.Params.AgentWidthCells > DoorRect.Min.X && Start.X < DoorRect.Max.X
						&& Start.Y + In.Params.AgentHeightCells > DoorRect.Min.Y && Start.Y < DoorRect.Max.Y;
				});
				if (!bOnStart)
				{
					DoorSpots.Add(Cell);
				}
			}
		}

		for (int32 I = DoorSpots.Num() - 1; I > 0; --I)
		{
			DoorSpots.Swap(I, Rng.RandRange(0, I));
		}

		// The puzzle follows the paths: doors on the main path first, then at the mouths of
		// branches, then anywhere; keys preferably out in the branches.
		FMazePathGraph Paths;
		const bool bPaths = MazePaths::Build(Base, In.Exits,
			FMath::Max3(2, In.Params.AgentWidthCells, In.Params.AgentHeightCells / 2), Paths);

		auto NodeAt = [&Paths, &Base, bPaths](const FIntPoint& Cell) -> const FMazePathNode*
		{
			if (!bPaths || !Base.IsInside(Cell.X, Cell.Y))
			{
				return nullptr;
			}
			const int32 Node = Paths.NodeOf[Base.Index(Cell.X, Cell.Y)];
			return Node == INDEX_NONE ? nullptr : &Paths.Nodes[Node];
		};

		auto DoorRank = [&](const FIntPoint& Cell)
		{
			const FMazePathNode* Node = NodeAt(Cell);
			if (!Node)
			{
				return 2;
			}
			if (Node->bMain)
			{
				return 0;
			}
			const bool bMouth = Node->Level == 1
				&& Node->Prev.ContainsByPredicate([&Paths](int32 P) { return Paths.Nodes[P].bMain; });
			return bMouth ? 1 : 2;
		};

		DoorSpots.StableSort([&DoorRank](const FIntPoint& A, const FIntPoint& B) { return DoorRank(A) < DoorRank(B); });

		bool bPlaced = false;
		constexpr int32 MaxTries = 200;
		for (int32 Try = 0; Try < DoorSpots.Num() && Try < MaxTries && !bPlaced; ++Try)
		{
			const FIntPoint DoorCell = DoorSpots[Try];
			const FMazeReachLock Door{ FIntRect(DoorCell, DoorCell + DoorFootprint), LockId };

			// The maze with this door shut and no key for it yet.
			FMazeReachParams Shut = In.Params;
			Shut.Doors.Add(Door);
			FMazeReachResult After;
			MazeReachability::Solve(Asset->Grid, Shut, After);

			int32 Locked = 0;
			for (int32 I = 0; I < Base.Size.X * Base.Size.Y; ++I)
			{
				Locked += (Base.Standable[I] && Base.Reached[I] && !After.Reached[I]) ? 1 : 0;
			}
			if (Locked < Settings.MinLockedCells
				|| Locked > Settings.MaxLockedShare * FMath::Max(1, Base.NumReachedStandable))
			{
				continue;
			}

			// Colours that come into reach in each round of doors, with this door shut.
			TMap<int32, TSet<FName>> ColoursByRound;
			for (int32 K = 0; K < In.Params.Keys.Num(); ++K)
			{
				if (After.KeyPass[K] != INDEX_NONE)
				{
					ColoursByRound.FindOrAdd(After.KeyPass[K]).Add(In.Params.Keys[K].LockId);
				}
			}

			// The key: anywhere still reachable with this door shut — behind other doors too, as
			// long as their keys can be had — and far enough from the door in rooms.
			const int32 DoorRoom = RoomIndexAt(Rooms, DoorCell);
			const TArray<int32> Distance = RoomDistances(Rooms, DoorRoom);

			TArray<FIntPoint> KeySpots;
			for (int32 Z = 1; Z < After.Size.Y; ++Z)
			{
				for (int32 X = 0; X < After.Size.X; ++X)
				{
					if (!After.IsReached(X, Z) || !After.IsStandable(X, Z)
						|| !StandsHere(After, FIntPoint(X, Z), KeyFootprint, 0))
					{
						continue;
					}

					if (DoorRoom != INDEX_NONE)
					{
						const int32 KeyRoom = RoomIndexAt(Rooms, FIntPoint(X, Z));
						const int32 Steps = KeyRoom == INDEX_NONE ? INDEX_NONE : Distance[KeyRoom];
						if (KeyRoom == INDEX_NONE || (Steps != INDEX_NONE && Steps - 1 < Settings.MinRoomsBetweenKeyAndDoor)
							|| (Steps != INDEX_NONE && Settings.MaxRoomsBetweenKeyAndDoor > 0
								&& Steps - 1 > Settings.MaxRoomsBetweenKeyAndDoor))
						{
							continue;
						}
					}

					// Behind no more doors in a row than allowed, and not one key too many at once.
					const int32 Round = After.ReachPass[After.Index(X, Z)];
					if (Round + 1 > Settings.MaxLockNesting)
					{
						continue;
					}
					const TSet<FName>* Colours = ColoursByRound.Find(Round);
					const int32 Held = (Colours ? Colours->Num() : 0) + ((Colours && Colours->Contains(LockId)) ? 0 : 1);
					if (Held > Settings.MaxKeysHeld)
					{
						continue;
					}

					KeySpots.Add(FIntPoint(X, Z));
				}
			}

			if (KeySpots.Num() == 0)
			{
				continue;
			}

			// Out in a branch when there is one to be had: a key on the main path is found by walking past it.
			TArray<FIntPoint> OffMain = KeySpots.FilterByPredicate([&NodeAt](const FIntPoint& Cell)
			{
				const FMazePathNode* Node = NodeAt(Cell);
				return Node && !Node->bMain;
			});
			const TArray<FIntPoint>& Pool = OffMain.Num() > 0 ? OffMain : KeySpots;
			const FIntPoint KeyCell = Pool[Rng.RandRange(0, Pool.Num() - 1)];

			FMazePlacement DoorPlacement;
			DoorPlacement.TypeId = Library->PickVariant(DoorType->TypeId,
				MazeLadders::SpotHash(DoorCell, static_cast<uint32>(Settings.LockSeed)));
			DoorPlacement.CellXZ = DoorCell;
			DoorPlacement.Band = EMazeDepthBand::Play;
			DoorPlacement.Anchor = EMazeAnchorKind::Floor;
			DoorPlacement.LockId = LockId;
			DoorPlacement.bFromLockGenerator = true;

			FMazePlacement KeyPlacement = DoorPlacement;
			KeyPlacement.TypeId = Library->PickVariant(KeyType->TypeId,
				MazeLadders::SpotHash(KeyCell, static_cast<uint32>(Settings.LockSeed)));
			KeyPlacement.CellXZ = KeyCell;

			Placed.Add(DoorPlacement);
			Placed.Add(KeyPlacement);
			In.Params.Doors.Add(Door);
			In.Params.Keys.Add({ FIntRect(KeyCell, KeyCell + KeyFootprint), LockId });

			++Made;
			bPlaced = true;

			UE_LOG(LogMazeForge, Log,
				TEXT("Generate locks: '%s' door at (%d, %d) closes off %d places to stand; its key at (%d, %d)."),
				*LockId.ToString(), DoorCell.X, DoorCell.Y, Locked, KeyCell.X, KeyCell.Y);
		}

		if (!bPlaced)
		{
			StoppedBecause = FString::Printf(
				TEXT(" Stopped at lock %d: no passage left where a door closes off at least %d places to "
				     "stand and a key can lie %d room(s) away without it — lower Min Locked Cells or Min "
				     "Rooms Between, or try another Lock Seed."),
				Lock + 1, Settings.MinLockedCells, Settings.MinRoomsBetweenKeyAndDoor);
			break;
		}
	}

	if (Placed.Num() > 0)
	{
		Spawns->AddBatch(Placed);
	}

	UE_LOG(LogMazeForge, Log,
		TEXT("Generate locks: %d of %d lock(s) placed, %d generated door(s) and key(s) from before removed.%s"),
		Made, Settings.LockCount, Removed, *StoppedBecause);

	Check(Settings);
}

void MazeReachabilityTool::ClearGeneratedLocks(UMazeEdModeSettings& Settings)
{
	UMazeGridAsset* Asset = Settings.TargetAsset.LoadSynchronous();
	UMazeSpawnAsset* Spawns = Asset ? Asset->Spawns.LoadSynchronous() : nullptr;
	if (!Spawns)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ClearLocks", "MazeForge: Clear Generated Locks"));
	const int32 Removed = Spawns->RemoveLockGenerated();
	UE_LOG(LogMazeForge, Log, TEXT("Clear generated locks: %d door(s) and key(s) removed; hand-placed ones kept."), Removed);

	Check(Settings);
}

void MazeReachabilityTool::AnalyzePaths(UMazeEdModeSettings& Settings)
{
	Settings.PathLines.Reset();
	Settings.PathPoints.Reset();

	const UMazeGridAsset* Asset = Settings.TargetAsset.LoadSynchronous();
	if (!Asset)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Analyze paths: no Target Asset set."));
		return;
	}

	const FInputs In = GatherInputs(Settings, *Asset);
	FMazeReachResult Result;
	MazeReachability::Solve(Asset->Grid, In.Params, Result);

	// A band about as deep as the player is wide: a corridor stays one node thick.
	const int32 Band = FMath::Max3(2, In.Params.AgentWidthCells, In.Params.AgentHeightCells / 2);

	FMazePathGraph Graph;
	if (!MazePaths::Build(Result, In.Exits, Band, Graph))
	{
		Settings.PathStatus = TEXT("nothing reached — no path to analyse");
		UE_LOG(LogMazeForge, Warning, TEXT("Analyze paths: nothing reachable from the start."));
		return;
	}

	const float Lift = In.Params.AgentHeightCells * 0.5f;
	for (const FMazePathNode& Node : Graph.Nodes)
	{
		const FVector2f From = Node.Centre + FVector2f(0.0f, Lift);
		for (const int32 Next : Node.Next)
		{
			const FMazePathNode& To = Graph.Nodes[Next];
			FMazePathOverlayLine Line;
			Line.A = From;
			Line.B = To.Centre + FVector2f(0.0f, Lift);
			Line.Kind = (Node.bMain && To.bMain) ? 0 : static_cast<uint8>(FMath::Clamp(To.Level, 1, 4));
			Settings.PathLines.Add(Line);
		}

		if (!Node.bMain && !Node.Next.ContainsByPredicate([&Graph](int32 N) { return !Graph.Nodes[N].bMain; }))
		{
			Settings.PathPoints.Add({ From, 1 });
		}
		else if (Node.bMain && Node.Next.ContainsByPredicate([&Graph](int32 N) { return !Graph.Nodes[N].bMain; }))
		{
			Settings.PathPoints.Add({ From, 2 });
		}
	}

	Settings.PathPoints.Add({ FVector2f(Graph.Stats.Start.X + 0.5f, Graph.Stats.Start.Y + Lift), 3 });
	Settings.PathPoints.Add({ FVector2f(Graph.Stats.Exit.X + 0.5f, Graph.Stats.Exit.Y + Lift), 4 });

	Settings.ReachAsset = Asset;
	Settings.PathStatus = Graph.Stats.ToString();

	UE_LOG(LogMazeForge, Log, TEXT("Analyze paths: %s.%s"), *Settings.PathStatus,
		Graph.Stats.bExitFromGate ? TEXT("")
			: TEXT(" No Gate point reached: the main path ends at the farthest place — put the exit there, or a Gate where you want it."));

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports();
	}
}

namespace
{
	/** How far a set of path figures is from the targets: 0 — on target. Each target weighs one. */
	float PathPenalty(const FMazePathStats& Stats, const UMazeEdModeSettings& Settings)
	{
		float Penalty = 0.0f;
		if (Settings.TargetDeadEnds > 0)
		{
			Penalty += FMath::Abs(Stats.DeadEnds - Settings.TargetDeadEnds) / float(Settings.TargetDeadEnds);
		}
		if (Settings.TargetMinBranchDepth > 0 && Stats.AvgBranchDepth < Settings.TargetMinBranchDepth)
		{
			Penalty += (Settings.TargetMinBranchDepth - Stats.AvgBranchDepth) / float(Settings.TargetMinBranchDepth);
		}
		if (Settings.TargetNesting > 0)
		{
			Penalty += FMath::Abs(Stats.MaxNesting - Settings.TargetNesting) / float(Settings.TargetNesting);
		}
		if (Settings.TargetMinConfusion > 0 && Stats.Confusion < Settings.TargetMinConfusion)
		{
			Penalty += (Settings.TargetMinConfusion - Stats.Confusion) / float(Settings.TargetMinConfusion);
		}
		return Penalty;
	}

	bool BuildPaths(const UMazeEdModeSettings& Settings, const UMazeGridAsset& Asset, FMazePathGraph& OutGraph)
	{
		const FInputs In = GatherInputs(Settings, Asset);
		FMazeReachResult Result;
		MazeReachability::Solve(Asset.Grid, In.Params, Result);
		const int32 Band = FMath::Max3(2, In.Params.AgentWidthCells, In.Params.AgentHeightCells / 2);
		return MazePaths::Build(Result, In.Exits, Band, OutGraph);
	}
}

void MazeReachabilityTool::FindSeed(UMazeEdModeSettings& Settings)
{
	UMazeGridAsset* Asset = Settings.TargetAsset.LoadSynchronous();
	UMazeGeneratorBase* Generator = Asset ? Asset->Generator.Get() : nullptr;
	if (!Generator || Settings.IsGeneratorManual())
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Find seed: the target has no generator to try seeds with."));
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("FindSeed", "MazeForge: Find Seed"));
	Asset->Modify();
	Generator->Modify();

	const int32 FirstSeed = Generator->Seed;
	FRandomStream Seeds(FirstSeed);
	int32 BestSeed = FirstSeed;
	float BestPenalty = TNumericLimits<float>::Max();
	FMazePathStats BestStats;
	int32 Tried = 0;

	FScopedSlowTask Progress(static_cast<float>(Settings.SeedTries), LOCTEXT("FindSeedTask", "Trying seeds..."));
	Progress.MakeDialog(/*bShowCancelButton*/ true);

	for (int32 Try = 0; Try < Settings.SeedTries && !Progress.ShouldCancel(); ++Try)
	{
		Progress.EnterProgressFrame(1.0f);

		const int32 Seed = Try == 0 ? FirstSeed : Seeds.RandRange(1, MAX_int32 - 1);
		Generator->Seed = Seed;
		Asset->RunGenerator();
		++Tried;

		FMazePathGraph Graph;
		if (!BuildPaths(Settings, *Asset, Graph))
		{
			continue;
		}

		const float Penalty = PathPenalty(Graph.Stats, Settings);
		UE_LOG(LogMazeForge, Verbose, TEXT("Find seed: %d — penalty %.2f — %s"), Seed, Penalty, *Graph.Stats.ToString());
		if (Penalty < BestPenalty)
		{
			BestPenalty = Penalty;
			BestSeed = Seed;
			BestStats = Graph.Stats;
		}
		if (BestPenalty <= 0.0f)
		{
			break;
		}
	}

	// The winner, built for real.
	Generator->Seed = BestSeed;
	Asset->RunGenerator();

	UE_LOG(LogMazeForge, Log,
		TEXT("Find seed: %d seed(s) tried, kept %d (%s, distance from the targets %.2f): %s."),
		Tried, BestSeed, BestPenalty <= 0.0f ? TEXT("on target") : TEXT("closest"), BestPenalty,
		*BestStats.ToString());

	AnalyzePaths(Settings);
}

#undef LOCTEXT_NAMESPACE
