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
 *  Where one maze has been dragged to on the world map.
 *
 *  Kept in a list of its own rather than added to the Mazes array, and that is about the graphs
 *  that already exist: changing the type of an array a designer has filled in loses what is in
 *  it on the next load. A maze with no entry here is laid out automatically, which is also what
 *  every maze does until somebody drags it.
 *
 *  Canvas coordinates, not world ones. The map is a diagram of how the game is wired, not a
 *  plan of where its mazes sit in space — two mazes can and do occupy the same world coordinates
 *  while only one of them is loaded.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeWorldLayoutEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TSoftObjectPtr<UMazeWorldManifest> Maze;

	/** Top-left of the maze's schematic, in the map's own coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	FVector2D Position = FVector2D::ZeroVector;
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

	/**
	 *  Where mazes have been dragged to. Sparse: only the ones that were moved.
	 *
	 *  Editor furniture, and it changes nothing about how the game runs — a world with every
	 *  maze piled in one corner plays exactly like a tidy one. It lives on the asset because
	 *  the arrangement is somebody's understanding of their own game, and losing it when the
	 *  window closes would make arranging it pointless.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TArray<FMazeWorldLayoutEntry> Layout;

	/** Where the link starting at this gate leads, or null. */
	const FMazeWorldLink* FindLinkFrom(const UMazeWorldManifest* FromMaze, int32 FromId) const;

	/** Whether this maze is on the map at all. */
	bool HasMaze(const UMazeWorldManifest* Manifest) const;

	/** Where this maze was dragged to, or null if it has never been moved. */
	const FVector2D* FindLayout(const UMazeWorldManifest* Manifest) const;

	/** Records a drag. Adds an entry if there was none. Does not mark the package dirty. */
	void SetLayout(const UMazeWorldManifest* Manifest, const FVector2D& Position);

	/** Forgets every hand placement, so the next draw lays everything out again. */
	void ClearLayout();

	/** The editor and the world map listen: the graph changed, redraw. */
	DECLARE_MULTICAST_DELEGATE(FOnMazeWorldGraphChanged);
	FOnMazeWorldGraphChanged OnGraphChanged;

	void NotifyGraphChanged();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
};
