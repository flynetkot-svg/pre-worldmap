#include "Assets/MazeSpawnRulesAsset.h"

#include "MazeForgeCore.h"

bool FMazeRoomFilter::Matches(const int32 Exits, const bool bHasTransition) const
{
	// Zero means "no limit" and not "zero exits", which is worth saying out loud because the
	// other reading is the dangerous one: a filter left at its defaults would then match only
	// rooms with no way out — no room at all — and the rule would place nothing while looking
	// perfectly reasonable in the panel.
	if (MinExits > 0 && Exits < MinExits)
	{
		return false;
	}

	if (MaxExits > 0 && Exits > MaxExits)
	{
		return false;
	}

	switch (Transitions)
	{
	case EMazeRoomTransitions::Only:
		return bHasTransition;

	case EMazeRoomTransitions::Never:
		return !bHasTransition;

	default:
		return true;
	}
}

FString FMazeSpawnRule::Describe() const
{
	if (Target == EMazeRuleTarget::Category)
	{
		const UEnum* Names = StaticEnum<EMazeObjectCategory>();

		return FString::Printf(TEXT("category %s"),
			Names ? *Names->GetNameStringByValue(static_cast<int64>(Category)) : TEXT("?"));
	}

	return FString::Printf(TEXT("'%s'"), *TypeId.ToString());
}

void UMazeSpawnRulesAsset::GetActiveRules(TArray<FMazeSpawnRule>& OutRules) const
{
	OutRules.Reset();

	for (const FMazeSpawnRule& Rule : Rules)
	{
		if (!Rule.bEnabled)
		{
			continue;
		}

		if (Rule.Target == EMazeRuleTarget::Type && Rule.TypeId.IsNone())
		{
			continue;
		}

		OutRules.Add(Rule);
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
				TEXT("%s: rule %s has Max %d below Min %d. %d will be used for both."),
				*GetName(), *Rule.Describe(),
				Rule.MaxPerRoom, Rule.MinPerRoom, Rule.MinPerRoom);
		}

		// The same kind of typo one field along, and a worse one: no room can have both more
		// exits than the maximum and fewer than the minimum, so this rule matches nothing in
		// any maze and places nothing, for ever, without a word.
		if (Rule.Rooms.MaxExits > 0 && Rule.Rooms.MinExits > Rule.Rooms.MaxExits)
		{
			UE_LOG(LogMazeForge, Warning,
				TEXT("%s: rule %s wants at least %d exits and at most %d. No room can be both, "
				     "so it will place nothing."),
				*GetName(), *Rule.Describe(), Rule.Rooms.MinExits, Rule.Rooms.MaxExits);
		}
	}
}
#endif
