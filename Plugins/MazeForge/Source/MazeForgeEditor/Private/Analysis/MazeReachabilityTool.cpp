#include "Analysis/MazeReachabilityTool.h"

#include "Analysis/MazeReachability.h"
#include "Assets/MazeGridAsset.h"
#include "Assets/MazeObjectLibrary.h"
#include "Assets/MazeSpawnAsset.h"
#include "Data/MazeRoomDesc.h"
#include "Editor.h"
#include "Generators/MazeGenerator_Classic.h"
#include "Misc/ScopedSlowTask.h"
#include "Slicers/MazeRoomSlicerBase.h"
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

			if (Type->Category == EMazeObjectCategory::Climb)
			{
				In.Params.Climbables.Add(FIntRect(Placement.CellXZ, Placement.MaxCellXZ(Type->FootprintCells)));
			}

			if (Type->TransitionRole == EMazeTransitionRole::Entry)
			{
				In.Params.Starts.Add(Placement.CellXZ);
				++In.EntryCount;
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
				// Stacked pieces of the one ladder type, as many as the climb is tall. Free
				// anchor: only the lowest has a floor under it.
				for (int32 Z = 0; Z < Connection.LadderSpan; Z += LadderFootprint.Y)
				{
					FMazePlacement Piece;
					Piece.TypeId = Ladder;
					Piece.CellXZ = FIntPoint(Connection.LadderBase.X, Connection.LadderBase.Y + Z);
					Piece.Band = EMazeDepthBand::Play;
					Piece.Anchor = EMazeAnchorKind::Free;
					Piece.bStructural = true;
					NewLadders.Add(Piece);
					In.Params.Climbables.Add(FIntRect(Piece.CellXZ, Piece.CellXZ + LadderFootprint));
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
			if (Locked < Settings.MinLockedCells)
			{
				continue;
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
						if (KeyRoom == INDEX_NONE || (Steps != INDEX_NONE && Steps - 1 < Settings.MinRoomsBetweenKeyAndDoor))
						{
							continue;
						}
					}

					KeySpots.Add(FIntPoint(X, Z));
				}
			}

			if (KeySpots.Num() == 0)
			{
				continue;
			}

			const FIntPoint KeyCell = KeySpots[Rng.RandRange(0, KeySpots.Num() - 1)];

			FMazePlacement DoorPlacement;
			DoorPlacement.TypeId = DoorType->TypeId;
			DoorPlacement.CellXZ = DoorCell;
			DoorPlacement.Band = EMazeDepthBand::Play;
			DoorPlacement.Anchor = EMazeAnchorKind::Floor;
			DoorPlacement.LockId = LockId;
			DoorPlacement.bFromLockGenerator = true;

			FMazePlacement KeyPlacement = DoorPlacement;
			KeyPlacement.TypeId = KeyType->TypeId;
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

#undef LOCTEXT_NAMESPACE
