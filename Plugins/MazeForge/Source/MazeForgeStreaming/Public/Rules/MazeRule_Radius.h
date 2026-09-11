#pragma once

#include "CoreMinimal.h"
#include "Rules/MazeStreamingRule.h"
#include "MazeRule_Radius.generated.h"

/** A safety net: plain proximity to the player. Keeps a ring of loaded rooms around him. */
UCLASS(DisplayName = "Player Radius")
class MAZEFORGESTREAMING_API UMazeRule_Radius : public UMazeStreamingRule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", Units = "cm"))
	float RadiusUU = 6000.0f;

	virtual void Score(const FMazeStreamQuery& Query,
	                   const UMazeWorldManifest& Manifest,
	                   TMap<FName, float>& InOutDesire) const override;

	virtual FText GetDisplayName() const override;
};
