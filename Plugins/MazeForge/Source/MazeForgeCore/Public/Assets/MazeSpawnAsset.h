#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/MazeSpawnTypes.h"
#include "MazeSpawnAsset.generated.h"

class UMazeObjectLibrary;

/**
 *  Where the objects of one maze stand.
 *
 *  A separate asset from the grid, and the reason is lifetime rather than tidiness. The maze
 *  gets generated, cleared, restored and redrawn many times; the decor pass is a separate and
 *  far more expensive investment made later. Data that lives at a different cadence and
 *  survives different operations belongs in a different container — the same argument that
 *  separated the build settings from the grid.
 *
 *  Concretely: Clear All Changes erases the drawing and leaves this alone.
 */
UCLASS(BlueprintType)
class MAZEFORGECORE_API UMazeSpawnAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** The object kinds these placements refer to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner")
	TSoftObjectPtr<UMazeObjectLibrary> Library;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner")
	TArray<FMazePlacement> Placements;

	/**
	 *  Next identity to hand out. Only ever goes up.
	 *
	 *  A counter and not the array length: deleting a placement must not renumber the others,
	 *  and a number that has been used must never come back. The ids key a runtime registry of
	 *  what the player has done to each object, so a reused number is a save pointing at the
	 *  wrong thing.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner")
	int32 NextId = 1;

	// ------------------------------------------------------------------- editing

	/** Appends a placement. It gets no id — that happens at the first export. */
	int32 Add(const FMazePlacement& Placement);

	/**
	 *  Appends many at once, under one transaction. Returns how many were added.
	 *
	 *  The brush adds one placement per click, so Add opening its own transaction costs
	 *  nothing there. The generator adds hundreds in a single action, and hundreds of
	 *  transactions is an undo history the designer has to press Ctrl+Z through one crate at a
	 *  time — for something they think of as one button.
	 */
	int32 AddBatch(const TArray<FMazePlacement>& NewPlacements);

	/** Removes the placement at this index. Ids of the rest are untouched. */
	void RemoveAt(int32 Index);

	/**
	 *  Drops every placement the generator made, and only those.
	 *
	 *  What makes regenerating safe: hand-made placements — transition points above all — are
	 *  never touched, whatever the rules say or how often they are run.
	 */
	int32 RemoveGenerated();

	/**
	 *  The topmost placement whose footprint covers this cell, or INDEX_NONE.
	 *
	 *  Topmost meaning last added, because that is what the eraser should take first: the thing
	 *  you just put down is the thing you are most likely undoing.
	 */
	int32 FindAtCell(const UMazeObjectLibrary* InLibrary, const FIntPoint& CellXZ) const;

	/** Everything goes. This is what the panel's Clear All Objects is for. */
	void ClearAll();

	/**
	 *  Hands the placement at this index an id if it has none, and returns the id it now has.
	 *
	 *  Called by the export, never by the brush, and one placement at a time rather than all of
	 *  them at once. Drawing costs nothing and creates nothing, so a point that has never been
	 *  built has nothing to identify; a point that HAS been built keeps its number for ever,
	 *  because somewhere there may be a save that names it. And a placement the export refused
	 *  to build gets no number at all — otherwise "has an id" would stop meaning "exists in the
	 *  world" the first time a wall was redrawn under a crate.
	 */
	int32 AssignId(int32 Index);

	/** How many placements have never been exported. */
	int32 CountUnassigned() const;

	/** The editor and the preview listen to it: the placements changed, redraw. */
	DECLARE_MULTICAST_DELEGATE(FOnMazeSpawnsChanged);
	FOnMazeSpawnsChanged OnSpawnsChanged;

	void NotifySpawnsChanged();
};
