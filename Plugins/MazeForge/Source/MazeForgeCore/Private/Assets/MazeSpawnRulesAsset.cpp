#include "Assets/MazeSpawnRulesAsset.h"

#include "MazeForgeCore.h"

void UMazeSpawnRulesAsset::GetActiveRules(TArray<FMazeSpawnRule>& OutRules) const
{
	OutRules.Reset();

	for (const FMazeSpawnRule& Rule : Rules)
	{
		if (Rule.bEnabled && !Rule.TypeId.IsNone())
		{
			OutRules.Add(Rule);
		}
	}
}

#if WITH_EDITOR
void UMazeSpawnRulesAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Said once, on edit, rather than on every generation run. A maximum below the minimum is
	// not worth refusing over — the generator treats it as the minimum — but it is worth
	// hearing about, because it is always a typo and the result looks like the rule was
	// ignored.
	for (const FMazeSpawnRule& Rule : Rules)
	{
		if (Rule.MaxPerRoom < Rule.MinPerRoom)
		{
			UE_LOG(LogMazeForge, Warning,
				TEXT("%s: rule '%s' has Max %d below Min %d. %d will be used for both."),
				*GetName(), *Rule.TypeId.ToString(),
				Rule.MaxPerRoom, Rule.MinPerRoom, Rule.MinPerRoom);
		}
	}
}
#endif
