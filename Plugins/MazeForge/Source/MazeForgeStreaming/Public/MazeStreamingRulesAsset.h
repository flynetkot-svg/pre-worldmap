#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MazeStreamingRulesAsset.generated.h"

class UMazeStreamingRule;

/**
 *  The rules and budgets of the load pool.
 *
 *  A separate asset so that the testers can tweak the thresholds and limits without
 *  a rebuild and compare the behaviour on one and the same map.
 */
UCLASS(BlueprintType)
class MAZEFORGESTREAMING_API UMazeStreamingRulesAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UMazeStreamingRulesAsset();

	/** The set of rules. Each writes its own "desire" to keep a room loaded. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Rules")
	TArray<TObjectPtr<UMazeStreamingRule>> Rules;

	/** Above this threshold we start loading a room. */
	UPROPERTY(EditAnywhere, Category = "Budget", meta = (ClampMin = "0", ClampMax = "1"))
	float LoadThreshold = 0.35f;

	/**
	 *  Below this threshold we unload.
	 *
	 *  Two different thresholds are the hysteresis. With a single one, a room on the
	 *  border would load and unload every frame.
	 */
	UPROPERTY(EditAnywhere, Category = "Budget", meta = (ClampMin = "0", ClampMax = "1"))
	float UnloadThreshold = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxLoadedRooms = 9;

	/** How many loads run at once. More means a sharper hitch on disk. */
	UPROPERTY(EditAnywhere, Category = "Budget", meta = (ClampMin = "1"))
	int32 MaxConcurrentLoads = 2;

	/** The minimum lifetime of a loaded room. The second safeguard against flicker. */
	UPROPERTY(EditAnywhere, Category = "Budget", meta = (ClampMin = "0", Units = "s"))
	float MinTimeLoadedSeconds = 2.0f;

	/** How often to recompute the priorities. 10 Hz is enough and does not heat the profiler. */
	UPROPERTY(EditAnywhere, Category = "Budget", meta = (ClampMin = "0.01", Units = "s"))
	float UpdateIntervalSeconds = 0.1f;
};
