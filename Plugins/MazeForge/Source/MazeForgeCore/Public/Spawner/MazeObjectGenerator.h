#pragma once

#include "CoreMinimal.h"

class UMazeGridAsset;
class UMazeSpawnAsset;
class UMazeSpawnRulesAsset;

/** What one generation pass did, for the line the panel prints afterwards. */
struct MAZEFORGECORE_API FMazeGenerationReport
{
	/** Placements the pass created. */
	int32 Placed = 0;

	/** Placements from the previous pass that it removed first. */
	int32 Removed = 0;

	int32 Rooms = 0;
	int32 RulesApplied = 0;

	/**
	 *  How many times a room could not fit even the rule's minimum.
	 *
	 *  Not an error. A corridor one cell high has nowhere to stand a crate, and a rule asking
	 *  for two of them there is a rule meeting a maze, not a fault. It is counted because the
	 *  alternative is a designer wondering why half the rooms look empty.
	 */
	int32 ShortOfMinimum = 0;

	/** Rules naming a type the library does not have. Always a typo, always worth saying. */
	int32 UnknownTypes = 0;

	FString Describe() const;
};

/**
 *  Scattering objects through a maze according to rules.
 *
 *  Works room by room, because that is the unit a designer thinks in and the unit the rules are
 *  written in. Within a room it collects every cell the type could legally stand in, shuffles
 *  them, and takes them in that order until the room has its quota or the candidates run out.
 *
 *  Collect-then-shuffle rather than dart-throwing at random cells. Throwing darts is cheaper
 *  per attempt and unbounded in the worst case: in a room with three legal spots and a rule
 *  asking for three, the last dart spends thousands of throws finding the one cell left. The
 *  cost of listing the candidates is paid once and is the same whether the room is crowded or
 *  empty.
 */
namespace MazeObjectGenerator
{
	/**
	 *  Furnishes every room of this maze. Existing generated placements are replaced;
	 *  hand-placed ones are kept and treated as occupied ground.
	 *
	 *  The grid asset supplies the geometry and the room list, the spawn asset supplies the
	 *  object library and receives the result. Neither is saved here — the caller decides when.
	 */
	MAZEFORGECORE_API void Generate(const UMazeGridAsset& Asset, UMazeSpawnAsset& Spawns,
	                                const UMazeSpawnRulesAsset& Rules,
	                                FMazeGenerationReport& OutReport);
}
