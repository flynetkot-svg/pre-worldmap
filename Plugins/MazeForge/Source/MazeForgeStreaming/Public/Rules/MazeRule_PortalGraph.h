#pragma once

#include "CoreMinimal.h"
#include "Rules/MazeStreamingRule.h"
#include "MazeRule_PortalGraph.generated.h"

/**
 *  Proximity along the portal graph rather than through the air.
 *
 *  The most reliable rule in a maze: behind a wall a room can be a metre away from the
 *  player but a hundred metres of travel away. A breadth-first walk from the current
 *  room over the neighbour list from the manifest gives the real reachability; the other
 *  rules complement it.
 */
UCLASS(DisplayName = "Portal Graph")
class MAZEFORGESTREAMING_API UMazeRule_PortalGraph : public UMazeStreamingRule
{
	GENERATED_BODY()

public:
	/** How many transitions ahead to look. */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxDepth = 3;

	virtual void Score(const FMazeStreamQuery& Query,
	                   const UMazeWorldManifest& Manifest,
	                   TMap<FName, float>& InOutDesire) const override;

	virtual FText GetDisplayName() const override;
};
