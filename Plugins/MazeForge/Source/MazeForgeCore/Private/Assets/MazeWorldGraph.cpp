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

const FVector2D* UMazeWorldGraph::FindLayout(const UMazeWorldManifest* Manifest) const
{
	if (!Manifest)
	{
		return nullptr;
	}

	// By path, like FindLinkFrom and for the same reason: resolving the soft pointers would
	// load every manifest in the list to answer a question about one of them.
	const FSoftObjectPath Path(Manifest);

	const FMazeWorldLayoutEntry* Entry = Layout.FindByPredicate(
		[&Path](const FMazeWorldLayoutEntry& Candidate)
		{
			return Candidate.Maze.ToSoftObjectPath() == Path;
		});

	return Entry ? &Entry->Position : nullptr;
}

void UMazeWorldGraph::SetLayout(const UMazeWorldManifest* Manifest, const FVector2D& Position)
{
	if (!Manifest)
	{
		return;
	}

	const FSoftObjectPath Path(Manifest);

	FMazeWorldLayoutEntry* Entry = Layout.FindByPredicate(
		[&Path](const FMazeWorldLayoutEntry& Candidate)
		{
			return Candidate.Maze.ToSoftObjectPath() == Path;
		});

	if (Entry)
	{
		Entry->Position = Position;
		return;
	}

	FMazeWorldLayoutEntry Added;
	Added.Maze = const_cast<UMazeWorldManifest*>(Manifest);
	Added.Position = Position;

	Layout.Add(Added);
}

void UMazeWorldGraph::ClearLayout()
{
	Layout.Reset();
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
