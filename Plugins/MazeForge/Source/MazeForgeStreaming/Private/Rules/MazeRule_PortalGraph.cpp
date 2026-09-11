#include "Rules/MazeRule_PortalGraph.h"

#include "Assets/MazeWorldManifest.h"

#define LOCTEXT_NAMESPACE "MazeForge"

FText UMazeRule_PortalGraph::GetDisplayName() const
{
	return LOCTEXT("RulePortalGraph", "Portal Graph");
}

void UMazeRule_PortalGraph::Score(const FMazeStreamQuery& Query,
                                  const UMazeWorldManifest& Manifest,
                                  TMap<FName, float>& InOutDesire) const
{
	if (Query.CurrentRoomId.IsNone())
	{
		return;
	}

	TMap<FName, int32> Depths;
	Depths.Add(Query.CurrentRoomId, 0);

	TArray<FName> Frontier;
	Frontier.Add(Query.CurrentRoomId);

	for (int32 Depth = 0; Depth < MaxDepth && Frontier.Num() > 0; ++Depth)
	{
		TArray<FName> Next;

		for (const FName RoomId : Frontier)
		{
			const FMazeRoomEntry* Room = Manifest.FindRoom(RoomId);
			if (!Room)
			{
				continue;
			}

			for (const FName Neighbor : Room->Neighbors)
			{
				if (!Depths.Contains(Neighbor))
				{
					Depths.Add(Neighbor, Depth + 1);
					Next.Add(Neighbor);
				}
			}
		}

		Frontier = MoveTemp(Next);
	}

	for (const TPair<FName, int32>& Pair : Depths)
	{
		// The current room gets 1.0, the neighbours 0.5, the next ones 0.33 and so on.
		Accumulate(InOutDesire, Pair.Key, 1.0f / static_cast<float>(Pair.Value + 1));
	}
}

#undef LOCTEXT_NAMESPACE
