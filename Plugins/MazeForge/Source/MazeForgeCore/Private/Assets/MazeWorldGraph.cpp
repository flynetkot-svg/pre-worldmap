#include "Assets/MazeWorldGraph.h"

#include "Assets/MazeWorldManifest.h"
#include "MazeForgeCore.h"

const FMazeWorldLink* UMazeWorldGraph::FindLinkFrom(const UMazeWorldManifest* FromMaze,
                                                    const int32 FromId) const
{
	if (!FromMaze || FromId == 0)
	{
		return nullptr;
	}

	// Compared by path and not by resolving the soft pointer: the destination maze of every
	// other link would be loaded on the way past, and loading a manifest is loading the list
	// of levels for a maze nobody is going to.
	const FSoftObjectPath FromPath(FromMaze);

	return Links.FindByPredicate([&FromPath, FromId](const FMazeWorldLink& Link)
	{
		return Link.FromId == FromId && Link.FromMaze.ToSoftObjectPath() == FromPath;
	});
}

bool UMazeWorldGraph::HasMaze(const UMazeWorldManifest* Manifest) const
{
	if (!Manifest)
	{
		return false;
	}

	const FSoftObjectPath Path(Manifest);

	return Mazes.ContainsByPredicate([&Path](const TSoftObjectPtr<UMazeWorldManifest>& Entry)
	{
		return Entry.ToSoftObjectPath() == Path;
	});
}

void UMazeWorldGraph::NotifyGraphChanged()
{
	OnGraphChanged.Broadcast();
}

#if WITH_EDITOR
void UMazeWorldGraph::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);

	// Edited by hand in the details panel as well as by the world map, and the map has to
	// follow either. Without this, typing a number into the array leaves the picture stale.
	NotifyGraphChanged();
}
#endif
