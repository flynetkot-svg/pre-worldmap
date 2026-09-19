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
		TEXT("objects %d placed, %d from the previous run removed; %d rooms, %d rule runs; "
		     "%d rooms short of their minimum, %d rooms filtered out, %d rules matched no room, "
		     "%d rules match nothing in the library"),
		Placed, Removed, Rooms, RulesApplied, ShortOfMinimum, RoomsFiltered, RulesWithNoRooms,
		UnknownTypes);
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

	/**
	 *  Which types a rule is allowed to place: the one it names, or every one of its category.
	 *
	 *  Pointers into the library's own array, which is safe for exactly as long as nobody edits
	 *  the library during a pass — and nothing does; the generator reads it and writes only to
	 *  the spawn asset.
	 */
	void CollectRuleTypes(const UMazeObjectLibrary& Library, const FMazeSpawnRule& Rule,
	                      TArray<const FMazeObjectType*>& OutTypes)
	{
		OutTypes.Reset();

		if (Rule.Target == EMazeRuleTarget::Type)
		{
			if (const FMazeObjectType* Type = Library.FindType(Rule.TypeId))
			{
				OutTypes.Add(Type);
			}

			return;
		}

		for (const FMazeObjectType& Type : Library.Types)
		{
			if (Type.Category == Rule.Category && !Type.TypeId.IsNone())
			{
				OutTypes.Add(&Type);
			}
		}
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

	/**
	 *  Draws a type for each object the rule wants, and groups the draws by type.
	 *
	 *  Drawn per object because that is what makes a category rule worth having: "three decor"
	 *  should come out a crate, a barrel and a wardrobe. Grouped afterwards because listing the
	 *  cells a type can stand in is a walk over the whole room, and it should be paid once per
	 *  distinct type rather than once per object.
	 *
	 *  An array of pairs rather than a TMap, and that is not a preference. A TMap keyed by
	 *  pointer iterates in an order that depends on the addresses the library happens to sit
	 *  at, which differ between runs of the editor — the one thing a seeded generator must
	 *  never depend on. This keeps the order the draws came in.
	 *
	 *  A rule with a single type draws nothing at all. There is no choice to make, and spending
	 *  a number on it would shift every later draw in the room — which would rearrange the
	 *  furniture of every maze already made with these seeds, for no gain.
	 */
	void DrawQuota(const TArray<const FMazeObjectType*>& RuleTypes, const int32 Wanted,
	               FRandomStream& Rng, TArray<TPair<const FMazeObjectType*, int32>>& OutQuota)
	{
		OutQuota.Reset();

		for (int32 Item = 0; Item < Wanted; ++Item)
		{
			const FMazeObjectType* Picked = RuleTypes.Num() == 1
				? RuleTypes[0]
				: RuleTypes[Rng.RandRange(0, RuleTypes.Num() - 1)];

			TPair<const FMazeObjectType*, int32>* Existing = OutQuota.FindByPredicate(
				[Picked](const TPair<const FMazeObjectType*, int32>& Entry)
				{
					return Entry.Key == Picked;
				});

			if (Existing)
			{
				++Existing->Value;
			}
			else
			{
				OutQuota.Emplace(Picked, 1);
			}
		}
	}

	/**
	 *  The rooms holding a Gate or an Entry.
	 *
	 *  Worked out from the placements rather than stored on the room, for the same reason the
	 *  filter reads exits instead of tags: rooms are produced by the slicer and do not survive
	 *  a change of Room Size, while the objects standing in them do.
	 */
	void FindRoomsWithTransitions(const UMazeObjectLibrary& Library,
	                              const TArray<FMazeRoomDesc>& Rooms,
	                              const TArray<FMazePlacement>& Placements,
	                              TSet<FName>& OutRoomIds)
	{
		OutRoomIds.Reset();

		for (const FMazePlacement& Placement : Placements)
		{
			const FMazeObjectType* Type = Library.FindType(Placement.TypeId);
			if (!Type || Type->TransitionRole == EMazeTransitionRole::None)
			{
				continue;
			}

			for (const FMazeRoomDesc& Room : Rooms)
			{
				if (Room.ContainsXZ(Placement.CellXZ.X, Placement.CellXZ.Y))
				{
					OutRoomIds.Add(Room.RoomId);
					break;
				}
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
			TEXT("Generate: %s has no enabled rules that name anything. Nothing to do."),
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

	// Which types each rule may use does not depend on the room, so it is settled once. Doing
	// it inside the room loop counted the same typo once per room and printed a maze's worth of
	// identical complaints about a single misspelled name.
	TArray<TArray<const FMazeObjectType*>> TypesPerRule;
	TypesPerRule.SetNum(ActiveRules.Num());

	for (int32 RuleIndex = 0; RuleIndex < ActiveRules.Num(); ++RuleIndex)
	{
		CollectRuleTypes(*Library, ActiveRules[RuleIndex], TypesPerRule[RuleIndex]);

		// A transition point is never furniture, and the generator does not scatter it.
		//
		// It has an identity the rest of the project holds on to: its placement id is what the
		// world graph joins one maze's Gate to another maze's Entry with. Generated placements
		// are thrown away and made again on every run, so a generated transition comes back
		// each time with a new number, and the link in the graph dies without a word — the maze
		// still loads, the door still opens, and the player arrives nowhere.
		//
		// It matters now because a rule may name a category, and System is exactly the category
		// transitions live in. A rule naming a type could always have named a Gate, but nobody
		// writes "scatter three doors"; "scatter three System" is a sentence somebody writes by
		// accident on the way to placing traps.
		const int32 Refused = TypesPerRule[RuleIndex].RemoveAll(
			[](const FMazeObjectType* Type)
			{
				return Type->TransitionRole != EMazeTransitionRole::None;
			});

		if (Refused > 0)
		{
			UE_LOG(LogMazeForge, Warning,
				TEXT("Generate: rule %s covers %d transition type(s). They were left out — a "
				     "Gate or an Entry is placed by hand, because the world graph links it by "
				     "the id it keeps."),
				*ActiveRules[RuleIndex].Describe(), Refused);
		}

		if (TypesPerRule[RuleIndex].Num() == 0)
		{
			++OutReport.UnknownTypes;

			UE_LOG(LogMazeForge, Warning,
				TEXT("Generate: rule %s matches nothing in library %s, so it places nothing."),
				*ActiveRules[RuleIndex].Describe(), *Library->GetName());
		}
	}

	TSet<FName> RoomsWithTransition;
	FindRoomsWithTransitions(*Library, Asset.Rooms, Spawns.Placements, RoomsWithTransition);

	// How many rooms each rule actually ran in, so that a filter which fits nothing can say so
	// instead of leaving a designer to count objects and guess.
	TArray<int32> RoomsPerRule;
	RoomsPerRule.Init(0, ActiveRules.Num());

	TArray<FMazePlacement> Created;
	TArray<TPair<FIntPoint, EMazeAnchorKind>> Candidates;
	TArray<TPair<const FMazeObjectType*, int32>> Quota;

	OutReport.Rooms = Asset.Rooms.Num();

	for (const FMazeRoomDesc& Room : Asset.Rooms)
	{
		FRandomStream Rng = MakeRoomStream(Rules.Seed, Room.RoomId);

		const int32 Exits = Room.Neighbors.Num();
		const bool bHasTransition = RoomsWithTransition.Contains(Room.RoomId);

		for (int32 RuleIndex = 0; RuleIndex < ActiveRules.Num(); ++RuleIndex)
		{
			const FMazeSpawnRule& Rule = ActiveRules[RuleIndex];
			const TArray<const FMazeObjectType*>& RuleTypes = TypesPerRule[RuleIndex];

			if (RuleTypes.Num() == 0)
			{
				// Already complained about, once, above.
				continue;
			}

			if (!Rule.Rooms.Matches(Exits, bHasTransition))
			{
				++OutReport.RoomsFiltered;
				continue;
			}

			++RoomsPerRule[RuleIndex];
			++OutReport.RulesApplied;

			const int32 Minimum = FMath::Max(0, Rule.MinPerRoom);
			const int32 Maximum = FMath::Max(Minimum, Rule.MaxPerRoom);
			const int32 Wanted = Rng.RandRange(Minimum, Maximum);

			if (Wanted == 0)
			{
				continue;
			}

			const int32 SliceY = MazePlacement::BandSliceY(Grid, Rule.Band);

			DrawQuota(RuleTypes, Wanted, Rng, Quota);

			int32 PlacedHere = 0;

			for (const TPair<const FMazeObjectType*, int32>& Want : Quota)
			{
				const FMazeObjectType& Type = *Want.Key;

				CollectCandidates(Grid, Type, Room, SliceY, Candidates);
				Shuffle(Candidates, Rng);

				int32 PlacedOfType = 0;

				for (const TPair<FIntPoint, EMazeAnchorKind>& Candidate : Candidates)
				{
					if (PlacedOfType >= Want.Value)
					{
						break;
					}

					// The geometry said yes when the candidates were collected; this is the
					// other half, the one the placement rules cannot answer — is anything
					// already there.
					if (!Occupancy.IsClearOf(Candidate.Key, Type.FootprintCells,
						Rule.MinSpacingCells))
					{
						continue;
					}

					FMazePlacement Placement;
					Placement.TypeId = Type.TypeId;
					Placement.CellXZ = Candidate.Key;
					Placement.Band = Rule.Band;
					Placement.Anchor = Candidate.Value;
					Placement.bGenerated = true;
					Placement.Rotation = MazePlacement::ResolveRotation(
						Grid, Type, Candidate.Key, SliceY, Candidate.Value, Rules.Seed);

					Created.Add(Placement);

					// Marked immediately rather than after the room is finished: the next
					// candidate in this very loop has to see it, or two crates land on top of
					// each other with every rule satisfied.
					Occupancy.Add(Candidate.Key, Type.FootprintCells);

					++PlacedOfType;
					++PlacedHere;
				}
			}

			if (PlacedHere < Minimum)
			{
				++OutReport.ShortOfMinimum;

				UE_LOG(LogMazeForge, Verbose,
					TEXT("Generate: room %s fits only %d of the %d %s its rule asks for."),
					*Room.RoomId.ToString(), PlacedHere, Minimum, *Rule.Describe());
			}
		}
	}

	// A rule that ran nowhere is not a narrow rule, it is a rule that cannot fire: either its
	// filter contradicts itself or this maze has no room of the shape it was written for.
	// Either way it is the answer to "why did nothing appear", and it should not have to be
	// worked out by counting objects.
	for (int32 RuleIndex = 0; RuleIndex < ActiveRules.Num(); ++RuleIndex)
	{
		if (RoomsPerRule[RuleIndex] > 0 || TypesPerRule[RuleIndex].Num() == 0)
		{
			continue;
		}

		++OutReport.RulesWithNoRooms;

		UE_LOG(LogMazeForge, Warning,
			TEXT("Generate: no room in %s passes the filter of rule %s, so it placed nothing."),
			*Asset.GetName(), *ActiveRules[RuleIndex].Describe());
	}

	OutReport.Placed = Spawns.AddBatch(Created);

	UE_LOG(LogMazeForge, Log, TEXT("Generate: %s"), *OutReport.Describe());
}
