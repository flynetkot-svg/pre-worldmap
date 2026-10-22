#include "Analysis/MazeReachabilityTool.h"

#include "Analysis/MazeReachability.h"
#include "Assets/MazeGridAsset.h"
#include "Assets/MazeObjectLibrary.h"
#include "Assets/MazeSpawnAsset.h"
#include "Data/MazeRoomDesc.h"
#include "Editor.h"
#include "Generators/MazeGenerator_Classic.h"
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
	};

	/** Starts and ladders from the spawn asset, heights from the generator and the cell size. */
	FInputs GatherInputs(const UMazeEdModeSettings& Settings, const UMazeGridAsset& Asset)
	{
		FInputs In;
		In.Params.AgentHeightCells = Asset.Generator ? FMath::Max(1, Asset.Generator->AgentHeightCells) : 2;

		const double CellZ = FMath::Max(1.0, Asset.Grid.CellSize.Z);
		In.Params.ClimbUpCells = FMath::FloorToInt(FMath::Max(Settings.StepUpCm, Settings.JumpHeightCm) / CellZ);

		const UMazeSpawnAsset* Spawns = Asset.Spawns.LoadSynchronous();
		const UMazeObjectLibrary* Library = Spawns ? Spawns->Library.LoadSynchronous() : nullptr;
		if (!Spawns || !Library)
		{
			return In;
		}

		for (const FMazePlacement& Placement : Spawns->Placements)
		{
			const FMazeObjectType* Type = Library->FindType(Placement.TypeId);
			if (!Type)
			{
				continue;
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

	int32 Fine = 0;
	int32 Unreachable = 0;
	int32 Partial = 0;
	TArray<FString> UnreachableNames;

	for (const FMazeRoomDesc& Room : Asset->Rooms)
	{
		int32 Standable = 0;
		int32 Reached = 0;
		for (int32 Z = Room.MinXZ.Y; Z < Room.MaxXZ.Y; ++Z)
		{
			for (int32 X = Room.MinXZ.X; X < Room.MaxXZ.X; ++X)
			{
				if (Result.IsStandable(X, Z))
				{
					++Standable;
					Reached += Result.IsReached(X, Z) ? 1 : 0;
				}
			}
		}

		if (Standable == 0 || Reached == Standable)
		{
			++Fine;
		}
		else if (Reached == 0)
		{
			++Unreachable;
			UnreachableNames.Add(Room.RoomId.ToString());
			Settings.ReachRoomStatus.Add(Room.RoomId, 1);
		}
		else
		{
			++Partial;
			Settings.ReachRoomStatus.Add(Room.RoomId, 2);
		}
	}

	if (Asset->Rooms.Num() == 0)
	{
		for (const FMazeReachIsland& Island : Islands)
		{
			Settings.ReachIslandBounds.Add(FIntRect(Island.Min, Island.Max + FIntPoint(1, 1)));
		}
	}

	Settings.ReachAsset = Asset;

	const FString From = In.EntryCount > 0
		? FString::Printf(TEXT("from %d entry point(s)"), In.EntryCount)
		: (Result.UsedStarts.Num() > 0
			? FString::Printf(TEXT("from the lowest place to stand (%d, %d) — no Entry point placed"),
				Result.UsedStarts[0].X, Result.UsedStarts[0].Y)
			: FString(TEXT("from nowhere — there is no place to stand")));

	Settings.ReachabilityStatus = Islands.Num() == 0
		? FString::Printf(TEXT("all reachable (%d places to stand)"), Result.NumStandable)
		: FString::Printf(TEXT("%d unreachable piece(s); rooms: %d fine, %d unreachable, %d partly"),
			Islands.Num(), Fine, Unreachable, Partial);

	UE_LOG(LogMazeForge, Log,
		TEXT("Reachability %s, player %d cells tall, up %d cell(s) without a ladder, %d climbable(s): "
		     "%d of %d places to stand reached, %d unreachable piece(s). Rooms: %d fine, %d unreachable, "
		     "%d reached in part.%s"),
		*From, In.Params.AgentHeightCells, In.Params.ClimbUpCells, In.Params.Climbables.Num(),
		Result.NumReachedStandable, Result.NumStandable, Islands.Num(), Fine, Unreachable, Partial,
		UnreachableNames.Num() > 0 ? *(TEXT(" Unreachable: ") + FString::Join(UnreachableNames, TEXT(", "))) : TEXT(""));

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
	const FName Ladder = Spawns ? ResolveLadder(Settings, *Asset, LadderFootprint) : NAME_None;

	FInputs In = GatherInputs(Settings, *Asset);

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
				In.Params.AgentHeightCells, Settings.FixShaftWidthCells, !Ladder.IsNone(), Connection);

			if (!bOk)
			{
				for (const FIntPoint& Cell : Island.Cells)
				{
					Skipped.Add(Cell.Y * Result.Size.X + Cell.X);
				}
				(Ladder.IsNone() ? LeftUp : LeftOther) += 1;
				continue;
			}

			Carved += Connection.CarvedCells;
			if (Connection.bNeedsLadder)
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
				Ladder.IsNone() ? TEXT(" — reaching them means climbing, and there is no ladder type: set "
				                       "Fix Ladder Type, or Ladder Type on the Classic generator")
				                : TEXT(""))
			: TEXT(""));

	Check(Settings);
}

#undef LOCTEXT_NAMESPACE
