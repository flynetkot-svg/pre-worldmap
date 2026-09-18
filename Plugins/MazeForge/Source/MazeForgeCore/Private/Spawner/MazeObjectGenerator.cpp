#include "Spawner/MazeObjectGenerator.h"

#include "Assets/MazeGridAsset.h"
#include "Assets/MazeObjectLibrary.h"
#include "Assets/MazeSpawnAsset.h"
#include "Assets/MazeSpawnRulesAsset.h"
#include "Data/MazeGrid.h"
#include "Data/MazeRoomDesc.h"
#include "MazeForgeCore.h"
#include "Spawner/MazeOccupancy.h"
#include "Spawner/MazePlacementRules.h"

FString FMazeGenerationReport::Describe() const
{
	return FString::Printf(
		TEXT("objects %d placed, %d from the previous run removed; %d rooms, %d rules; "
		     "%d rooms short of their minimum, %d unknown types"),
		Placed, Removed, Rooms, RulesApplied, ShortOfMinimum, UnknownTypes);
}

namespace
{
	/**
	 *  A stream of its own per room.
	 *
	 *  Mixing the room id into the seed rather than running one stream across the whole maze,
	 *  so that what a room gets depends only on that room. With a single stream, adding one
	 *  crate to the first room reshuffles every room after it, and a designer who was happy
	 *  with room forty loses it by editing room two.
	 */
	FRandomStream MakeRoomStream(const int32 Seed, const FName RoomId)
	{
		return FRandomStream(Seed ^ static_cast<int32>(GetTypeHash(RoomId)));
	}

	/** Every cell of the room where this type could legally stand, with the anchor it would use. */
	void CollectCandidates(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                       const FMazeRoomDesc& Room, const int32 SliceY,
	                       TArray<TPair<FIntPoint, EMazeAnchorKind>>& OutCandidates)
	{
		OutCandidates.Reset();

		for (int32 X = Room.MinXZ.X; X < Room.MaxXZ.X; ++X)
		{
			for (int32 Z = Room.MinXZ.Y; Z < Room.MaxXZ.Y; ++Z)
			{
				const FIntPoint Cell(X, Z);

				EMazeAnchorKind Anchor = EMazeAnchorKind::Floor;
				if (MazePlacement::FindAnchor(Grid, Type, Cell, SliceY, Anchor))
				{
					OutCandidates.Emplace(Cell, Anchor);
				}
			}
		}
	}

	/**
	 *  Fisher-Yates, in place.
	 *
	 *  Written out rather than using FRandomStream's own shuffle helper because the whole point
	 *  of the seed is that two runs agree, and that guarantee has to belong to code we can see.
	 */
	void Shuffle(TArray<TPair<FIntPoint, EMazeAnchorKind>>& Candidates, FRandomStream& Rng)
	{
		for (int32 Index = Candidates.Num() - 1; Index > 0; --Index)
		{
			const int32 Swap = Rng.RandRange(0, Index);
			if (Swap != Index)
			{
				Candidates.Swap(Index, Swap);
			}
		}
	}
}

void MazeObjectGenerator::Generate(const UMazeGridAsset& Asset, UMazeSpawnAsset& Spawns,
                                   const UMazeSpawnRulesAsset& Rules,
                                   FMazeGenerationReport& OutReport)
{
	OutReport = FMazeGenerationReport();

	const UMazeObjectLibrary* Library = Spawns.Library.LoadSynchronous();
	if (!Library)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Generate: %s has no object library, so there is nothing to place."),
			*Spawns.GetName());
		return;
	}

	TArray<FMazeSpawnRule> ActiveRules;
	Rules.GetActiveRules(ActiveRules);

	if (ActiveRules.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Generate: %s has no enabled rules that name a type. Nothing to do."),
			*Rules.GetName());
		return;
	}

	if (Asset.Rooms.Num() == 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Generate: %s has no rooms. Press Slice Into Rooms first."), *Asset.GetName());
		return;
	}

	// The previous run goes first, so its objects do not count as occupied ground against this
	// one. Hand-placed ones stay, and the occupancy map is built from what survives.
	OutReport.Removed = Spawns.RemoveGenerated();

	const FMazeGrid& Grid = Asset.Grid;

	FMazeOccupancy Occupancy;
	Occupancy.AddPlacements(Library, Spawns.Placements);

	TArray<FMazePlacement> Created;
	TArray<TPair<FIntPoint, EMazeAnchorKind>> Candidates;

	OutReport.Rooms = Asset.Rooms.Num();

	for (const FMazeRoomDesc& Room : Asset.Rooms)
	{
		FRandomStream Rng = MakeRoomStream(Rules.Seed, Room.RoomId);

		for (const FMazeSpawnRule& Rule : ActiveRules)
		{
			const FMazeObjectType* Type = Library->FindType(Rule.TypeId);
			if (!Type)
			{
				// Counted once per room it is asked for, which overstates it; the message is
				// what matters and it names the type, so one look at the library fixes it.
				++OutReport.UnknownTypes;
				continue;
			}

			++OutReport.RulesApplied;

			const int32 Minimum = FMath::Max(0, Rule.MinPerRoom);
			const int32 Maximum = FMath::Max(Minimum, Rule.MaxPerRoom);
			const int32 Wanted = Rng.RandRange(Minimum, Maximum);

			if (Wanted == 0)
			{
				continue;
			}

			const int32 SliceY = MazePlacement::BandSliceY(Grid, Rule.Band);

			CollectCandidates(Grid, *Type, Room, SliceY, Candidates);
			Shuffle(Candidates, Rng);

			int32 PlacedHere = 0;

			for (const TPair<FIntPoint, EMazeAnchorKind>& Candidate : Candidates)
			{
				if (PlacedHere >= Wanted)
				{
					break;
				}

				// The geometry said yes when the candidates were collected; this is the other
				// half, the one the placement rules cannot answer — is anything already there.
				if (!Occupancy.IsClearOf(Candidate.Key, Type->FootprintCells, Rule.MinSpacingCells))
				{
					continue;
				}

				FMazePlacement Placement;
				Placement.TypeId = Rule.TypeId;
				Placement.CellXZ = Candidate.Key;
				Placement.Band = Rule.Band;
				Placement.Anchor = Candidate.Value;
				Placement.bGenerated = true;
				Placement.Rotation = MazePlacement::ResolveRotation(
					Grid, *Type, Candidate.Key, SliceY, Candidate.Value, Rules.Seed);

				Created.Add(Placement);

				// Marked immediately rather than after the room is finished: the next candidate
				// in this very loop has to see it, or two crates land on top of each other with
				// every rule satisfied.
				Occupancy.Add(Candidate.Key, Type->FootprintCells);

				++PlacedHere;
			}

			if (PlacedHere < Minimum)
			{
				++OutReport.ShortOfMinimum;

				UE_LOG(LogMazeForge, Verbose,
					TEXT("Generate: room %s fits only %d of the %d '%s' its rule asks for."),
					*Room.RoomId.ToString(), PlacedHere, Minimum, *Rule.TypeId.ToString());
			}
		}
	}

	OutReport.Placed = Spawns.AddBatch(Created);

	UE_LOG(LogMazeForge, Log, TEXT("Generate: %s"), *OutReport.Describe());
}
