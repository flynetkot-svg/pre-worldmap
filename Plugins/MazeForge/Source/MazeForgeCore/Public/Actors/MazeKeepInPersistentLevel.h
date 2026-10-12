#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MazeKeepInPersistentLevel.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UMazeKeepInPersistentLevel : public UInterface
{
	GENERATED_BODY()
};

/**
 *  "This actor belongs to the whole map, not to a room": Bind Decor To Rooms never moves it
 *  into a room level, wherever it stands.
 *
 *  For the managers a map runs on — weather, sound, music, anything a room switching off with
 *  its level would silence. The class list in Build Settings covers the engine's own (the sun,
 *  the sky, the fog); this is how a game's own classes say the same about themselves, once,
 *  instead of every map remembering to exclude them.
 *
 *  Implement it in C++ or in a Blueprint's Class Settings. The answer is asked per actor, so a
 *  class can expose it as a checkbox and let one instance travel with its room after all.
 *  Implementing the interface without overriding the function means "keep it".
 *
 *  Where it stands decides what "kept" means: in the persistent level it stays there; in a room
 *  level it is left alone; in the level of an abolished room it goes to the persistent level,
 *  which is the only place it is not lost.
 */
class MAZEFORGECORE_API IMazeKeepInPersistentLevel
{
	GENERATED_BODY()

public:
	/** True: never move this actor into a room level. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MazeForge")
	bool KeepInPersistentLevel() const;

	virtual bool KeepInPersistentLevel_Implementation() const { return true; }

	/** Whether an actor asks to stay, through the interface. False for any actor without it. */
	static bool Wants(const UObject* Object)
	{
		return Object && Object->Implements<UMazeKeepInPersistentLevel>()
			&& Execute_KeepInPersistentLevel(Object);
	}
};
