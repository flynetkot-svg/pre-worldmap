#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MazeWorldGraph.generated.h"

class UMazeWorldManifest;

/**
 *  One transition, in one direction.
 *
 *  Deliberately one-way. A door you can walk through both ways is two links, because that is
 *  what it is: two gates and two entries, and the way back does not have to arrive where the
 *  way there set off. Making it a single two-way object would hide that, and hide it exactly
 *  in the cases where it matters.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeWorldLink
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TSoftObjectPtr<UMazeWorldManifest> FromMaze;

	/** The placement id of the Gate this link starts at. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	int32 FromId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TSoftObjectPtr<UMazeWorldManifest> ToMaze;

	/** The placement id of the Entry this link arrives at. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	int32 ToId = 0;
};

/**
 *  How the mazes of one game join up.
 *
 *  The thing this asset exists to prevent: a door that knows its own destination. Bake the
 *  destination into the door and you need one door blueprint per doorway, the topology of the
 *  game lives scattered across blueprint defaults, and the question "is there a way out of
 *  TEST5 at all" has no way of being asked.
 *
 *  So the door knows only which door it is — its placement id, which it reads off its own
 *  UMazeObjectIdComponent — and asks this. One BP_Door serves every doorway in the game.
 *
 *  Links are keyed by placement id and not by name, and that is a trade with a sharp edge:
 *  delete a transition point and draw it again and it is a DIFFERENT point with a new number,
 *  so the link that named the old one is dead. That is honest — it really is a different door —
 *  but it means dead links have to be shown loudly rather than quietly ignored.
 */
UCLASS(BlueprintType)
class MAZEFORGECORE_API UMazeWorldGraph : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 *  The mazes this world is made of, by manifest.
	 *
	 *  The manifest and nothing else, deliberately. It already carries everything both halves
	 *  need — the room rectangles the map draws the schematic from, and the transition points it
	 *  draws as squares — so a second reference to the grid asset would be a second thing to
	 *  keep in step with the first, and the first is the only one the game can load.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TArray<TSoftObjectPtr<UMazeWorldManifest>> Mazes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TArray<FMazeWorldLink> Links;

	/** Where the link starting at this gate leads, or null. */
	const FMazeWorldLink* FindLinkFrom(const UMazeWorldManifest* FromMaze, int32 FromId) const;

	/** Whether this maze is on the map at all. */
	bool HasMaze(const UMazeWorldManifest* Manifest) const;

	/** The editor and the world map listen: the graph changed, redraw. */
	DECLARE_MULTICAST_DELEGATE(FOnMazeWorldGraphChanged);
	FOnMazeWorldGraphChanged OnGraphChanged;

	void NotifyGraphChanged();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
};
