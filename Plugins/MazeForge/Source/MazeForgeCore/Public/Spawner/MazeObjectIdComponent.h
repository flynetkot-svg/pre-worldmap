#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MazeObjectIdComponent.generated.h"

/**
 *  The identity the export stamps onto a spawned object.
 *
 *  It exists because the placement id lives in an editor asset and the game never opens editor
 *  assets: at runtime there is a streamed level with actors in it and nothing else. Something
 *  has to carry the number across that line, and this is the smallest thing that can.
 *
 *  A component and not an actor tag, although a tag would have cost no new code. The number is
 *  meant to key a registry of what the player has done to each object — opened this crate, broke
 *  that ladder — and a key that only exists as the tail of a string is a key nobody can typo-check.
 *  A field is read by GetPlacementId and a mistake is a compile error; a tag is read by parsing
 *  and a mistake is a save file quietly pointing at nothing.
 *
 *  The values are VisibleAnywhere on purpose. They are stamped by the export and read by the
 *  game; a hand-edited id would be a duplicate the moment the maze is exported again.
 */
UCLASS(ClassGroup = "MazeForge", meta = (DisplayName = "Maze Object Id"))
class MAZEFORGECORE_API UMazeObjectIdComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMazeObjectIdComponent();

	/**
	 *  The placement this actor was built from. Never zero on an exported object, never reused.
	 *
	 *  Zero means the actor was placed by hand in the level rather than by the export, which is
	 *  a legitimate thing to be — it just has no entry in the spawn asset to point back at.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MazeForge")
	int32 PlacementId = 0;

	/** Which library entry it came from. Kept so the game can ask what kind of thing this is. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MazeForge")
	FName TypeId;

	/** The room whose level holds it. Saves the game a bounds search to answer "where is this". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MazeForge")
	FName RoomId;

	/** The placement id of an actor, or 0 if it does not carry one. Null-safe. */
	UFUNCTION(BlueprintPure, Category = "MazeForge")
	static int32 GetPlacementId(const AActor* Actor);
};
