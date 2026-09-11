#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/MazeSpawnTypes.h"
#include "MazeObjectLibrary.generated.h"

/**
 *  The kinds of object the spawner knows how to place.
 *
 *  Shared across the whole project, exactly like the build settings and for the same reason: a
 *  lamp is the same lamp in every maze. What differs between mazes is where the lamps are, and
 *  that lives in the maze's own spawn asset.
 *
 *  One asset holding a list rather than one asset per type. It mirrors the surface palette,
 *  which has worked, and it keeps the whole content set in front of you while you author it.
 *  If it ever grows past a few dozen entries, splitting it is a data move, not a redesign.
 */
UCLASS(BlueprintType)
class MAZEFORGECORE_API UMazeObjectLibrary : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objects")
	TArray<FMazeObjectType> Types;

	/** The type with this id, or null. */
	const FMazeObjectType* FindType(FName TypeId) const;

	/** Every id in the library, in declaration order. For the brush palette. */
	void GetTypeIds(TArray<FName>& OutIds) const;

	/**
	 *  Ids that appear more than once, and entries with no id at all.
	 *
	 *  Both are silent poison: placements refer to types by name, so a duplicate id means half
	 *  of them resolve to the wrong object and nothing says which half. Reported in the log on
	 *  load and shown in the panel rather than left to be discovered in the level.
	 */
	void FindBrokenIds(TArray<FName>& OutDuplicates, int32& OutUnnamed) const;

#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** Writes the duplicate/unnamed report to the log. Called on load and after every edit. */
	void ReportBrokenIds() const;
};
