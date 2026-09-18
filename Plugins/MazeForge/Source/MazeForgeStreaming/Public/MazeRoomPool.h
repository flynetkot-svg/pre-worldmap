#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MazeStreamTypes.h"
#include "MazeRoomPool.generated.h"

class ULevelStreaming;
class UMazeStreamingRulesAsset;
class UMazeWorldManifest;

/** What the pool knows about a room right now. */
USTRUCT()
struct FMazeRoomRuntime
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<ULevelStreaming> Streaming;

	/**
	 *  The package this entry was resolved for.
	 *
	 *  Kept because the map is keyed by room id, and room ids are NOT unique between mazes:
	 *  every uniform slicing starts at R_000_000. An entry left over from another maze answers
	 *  to the name perfectly well and its Streaming pointer is valid, so without this the pool
	 *  would go on streaming the previous maze's rooms believing they were these.
	 */
	FName PackageName;

	float Desire = 0.0f;
	double LoadedAtSeconds = 0.0;
	double LoadStartedAtSeconds = 0.0;
	bool bWantsLoad = false;
};

/** A single load profile entry — what goes into the CSV for the testers. */
struct FMazeLoadSample
{
	FName RoomId;
	float Desire = 0.0f;
	double RequestedAt = 0.0;
	double CompletedAt = 0.0;

	double MillisecondsTaken() const { return (CompletedAt - RequestedAt) * 1000.0; }
};

/**
 *  The room load pool.
 *
 *  Computes desire from the rules, sorts, and within the budget toggles the
 *  ULevelStreaming flags. It never loads anything synchronously itself — it makes
 *  the decision, and the engine performs the load.
 */
UCLASS()
class MAZEFORGESTREAMING_API UMazeRoomPool : public UObject
{
	GENERATED_BODY()

public:
	void Update(const FMazeStreamQuery& Query,
	            const UMazeWorldManifest& Manifest,
	            const UMazeStreamingRulesAsset& RulesAsset,
	            UWorld& World);

	int32 GetLoadedCount() const;
	int32 GetStreamingMissCount() const { return StreamingMisses; }
	const TMap<FName, FMazeRoomRuntime>& GetRooms() const { return Rooms; }
	const TArray<FMazeLoadSample>& GetSamples() const { return Samples; }

	/** Writes the load profile to a CSV next to the project logs. Returns the path. */
	FString DumpSamplesToCsv() const;

	/**
	 *  Forgets every room — and unloads it on the way out.
	 *
	 *  The unload is the point, not the forgetting. Dropping the map alone leaves every level
	 *  of the previous maze standing in the world with nothing left that remembers asking for
	 *  them: the player arrives in the next maze while the last one is still there.
	 */
	void Reset();

private:
	UPROPERTY()
	TMap<FName, FMazeRoomRuntime> Rooms;

	TArray<FMazeLoadSample> Samples;

	/** Rooms already complained about: otherwise the warning would fire every update. */
	TSet<FName> WarnedRooms;

	int32 StreamingMisses = 0;

	/**
	 *  True until the first update after a Reset has finished.
	 *
	 *  A streaming miss means the player reached a room the pool should have had ready and did
	 *  not — a real fault, worth a warning. On the very first update there was nothing it could
	 *  have had ready: the pool has only just been told which maze it is looking at, and every
	 *  room in it is unloaded by definition. Counting that as a miss printed
	 *  "the player entered unloaded room R_000_000" on every single start of PIE, which taught
	 *  us to ignore the one line that was supposed to mean something.
	 */
	bool bFirstUpdateSinceReset = true;

	/**
	 *  Finds the streaming entry by the package name from the manifest.
	 *
	 *  The comparison is done on the name without the PIE prefix. In a packaged game a
	 *  missing entry is created on the fly; in PIE it returns nullptr and logs once,
	 *  because there is nothing to substitute for an unattached room there.
	 */
	ULevelStreaming* ResolveStreamingLevel(UWorld& World, const FName PackageName);
};
