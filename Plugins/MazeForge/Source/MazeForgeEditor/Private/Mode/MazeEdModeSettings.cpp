#include "Mode/MazeEdModeSettings.h"

#include "Assets/MazeGridAsset.h"
#include "Assets/MazeObjectLibrary.h"
#include "Assets/MazeSpawnAsset.h"
#include "Assets/MazeWorldGraph.h"
#include "Data/MazeGrid.h"
#include "Generators/MazeGeneratorBase.h"
#include "Generators/MazeGenerator_Manual.h"
#include "Export/MazeBakery.h"
#include "Export/MazeLevelAttacher.h"
#include "Export/MazeLevelExporter.h"
#include "Framework/Docking/TabManager.h"
#include "MazeForgeCore.h"
#include "Render/MazeEditorStyleAsset.h"
#include "World/SMazeWorldMap.h"

namespace
{
	/** The target, or null with a clear message: without it every button is meaningless. */
	UMazeGridAsset* RequireTarget(UMazeGridAsset* Asset, const TCHAR* Action)
	{
		if (!Asset)
		{
			UE_LOG(LogMazeForge, Warning, TEXT("%s: no Target Asset set."), Action);
		}

		return Asset;
	}
}

#if WITH_EDITOR
void UMazeEdModeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// We write immediately instead of on leaving the mode: the editor can go down unexpectedly,
	// and losing the chosen target and preset because of that would be a shame.
	SaveConfig();

	RefreshStatus();
	OnSettingsChanged.Broadcast();
}
#endif

const UMazeEditorStyleAsset& UMazeEdModeSettings::GetStyle() const
{
	// The CDO acts as the built-in preset: the default values of the class fields are exactly the
	// look we had before the asset existed. An empty Style field must not mean "no overlay".
	const UMazeEditorStyleAsset* Loaded = Style.LoadSynchronous();
	return Loaded ? *Loaded : *GetDefault<UMazeEditorStyleAsset>();
}

void UMazeEdModeSettings::RefreshStatus()
{
	const UMazeGridAsset* Asset = TargetAsset.LoadSynchronous();
	if (!Asset)
	{
		Status = TEXT("no target set");
		return;
	}

	// IsFlat() reports an empty grid as flat, and that reads like a statement about its shape.
	// A separate branch keeps the panel from lying about a freshly created asset.
	const TCHAR* Shape = (Asset->Grid.NumCells() == 0)
		? TEXT("empty")
		: (Asset->IsFlat() ? TEXT("plane") : TEXT("volume"));

	// The state of the meshes is visible at a glance: "stale" means the export will rebuild
	// them, and that is the answer to "why is this taking so long".
	// In rooms rather than yes/no: with an incremental bake "stale" was never the useful number.
	// "204/208 baked" says both that there is work to do and how little of it there is.
	const int32 FreshRooms = Asset->CountBakedRooms();

	FString Meshes;
	if (Asset->Rooms.Num() == 0)
	{
		Meshes = TEXT("no rooms");
	}
	else if (FreshRooms == Asset->Rooms.Num())
	{
		Meshes = TEXT("all baked");
	}
	else
	{
		Meshes = FString::Printf(TEXT("%d/%d baked"), FreshRooms, Asset->Rooms.Num());
	}

	Status = FString::Printf(TEXT("%s · cells %d · rooms %d · meshes: %s · snapshot: %s"),
		Shape,
		Asset->Grid.NumCells(),
		Asset->Rooms.Num(),
		*Meshes,
		Asset->SnapshotInfo.IsEmpty() ? TEXT("none") : *Asset->SnapshotInfo);
}

// ---------------------------------------------------------------------- objects

UMazeSpawnAsset* UMazeEdModeSettings::GetSpawnAsset() const
{
	const UMazeGridAsset* Asset = TargetAsset.LoadSynchronous();
	return Asset ? Asset->Spawns.LoadSynchronous() : nullptr;
}

UMazeObjectLibrary* UMazeEdModeSettings::GetObjectLibrary() const
{
	const UMazeSpawnAsset* Spawns = GetSpawnAsset();
	return Spawns ? Spawns->Library.LoadSynchronous() : nullptr;
}

void UMazeEdModeSettings::ClearAllObjects()
{
	UMazeSpawnAsset* Spawns = GetSpawnAsset();
	if (!Spawns)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Clear objects: the target has no Spawns asset set."));
		return;
	}

	Spawns->ClearAll();
	RefreshStatus();
	OnSettingsChanged.Broadcast();
}

// ------------------------------------------------------------- what is possible now

bool UMazeEdModeSettings::IsGeneratorManual() const
{
	const UMazeGridAsset* Asset = TargetAsset.LoadSynchronous();
	const UMazeGeneratorBase* Generator = Asset ? Asset->Generator : nullptr;

	// No target and no generator both count as manual: in either case pressing Generate would
	// do nothing, and a button that does nothing should say so rather than look ready.
	return !Generator || Generator->IsA<UMazeGenerator_Manual>();
}

FText UMazeEdModeSettings::GetGeneratorName() const
{
	const UMazeGridAsset* Asset = TargetAsset.LoadSynchronous();
	const UMazeGeneratorBase* Generator = Asset ? Asset->Generator : nullptr;

	return Generator ? Generator->GetDisplayName() : FText::FromString(TEXT("none"));
}

bool UMazeEdModeSettings::HasBuiltMaze() const
{
	const UMazeGridAsset* Asset = TargetAsset.LoadSynchronous();

	// Rooms and not cells. The edit cycle detaches levels and puts them back, and both halves are
	// meaningless until a slicing exists — a drawing that has never been built has no levels to
	// take out of the map.
	return Asset && Asset->Rooms.Num() > 0;
}

// ------------------------------------------------------------------ 0. edit cycle

void UMazeEdModeSettings::ChangeCurrentMaze()
{
	UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Change maze"));
	if (!Asset)
	{
		return;
	}

	// 1. The room levels come out of the map. While they are in it the preview deliberately draws
	//    nothing for those rooms — their real geometry is already on screen — so editing with them
	//    attached means editing something you cannot see.
	const int32 Detached = FMazeLevelAttacher::DetachRooms(Asset);

	// 2. Back to one cell per column. Lossless: a column that had any mass keeps a cell, and the
	//    back wall stays in its own slice. Painting in the volume would work too, but every stroke
	//    would touch the full depth, and on a maze this size that is felt.
	Asset->FlattenToPlane();

	// 3. The cursor is put on the plane that was just built. Without this the active slice is
	//    wherever it was left, and the first stroke lands in a slice nobody is looking at.
	ActiveDepthSlice = Asset->Grid.Depth.PlaneCellY();

	RefreshStatus();
	OnSettingsChanged.Broadcast();

	UE_LOG(LogMazeForge, Log,
		TEXT("Change maze: %d levels detached, the grid is flat, drawing slice Y %d. "
		     "Edit, then press Apply Changes To Current Maze."),
		Detached, ActiveDepthSlice);
}

void UMazeEdModeSettings::ApplyChanges()
{
	// Deliberately the same work as Apply Changes To Current Maze. The two differ in what they
	// mean, not in what they do: this one is the first build of something that has only been
	// drawn, that one closes an edit cycle. Sharing the implementation is right; sharing the
	// button would make the label wrong half the time.
	ApplyChangesToCurrentMaze();
}

void UMazeEdModeSettings::ApplyChangesToCurrentMaze()
{
	UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Apply changes"));
	if (!Asset)
	{
		return;
	}

	if (Asset->Grid.NumCells() == 0)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Apply changes: the grid is empty. Nothing to build."));
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	// 1. The snapshot is taken of the PLANE, before the volume is grown — that is the state worth
	//    coming back to. A snapshot of the volume would restore something that then has to be
	//    flattened again before it can be edited.
	Asset->SaveSnapshot();

	// 2-3. Shape, then rooms.
	Asset->BuildDepthVolume();
	Asset->SliceIntoRooms();

	// 4. Only the rooms whose contents moved. This is the step that used to take minutes on a
	//    208-room maze for the sake of one edited corner.
	FMazeBakeReport BakeReport;
	FMazeBakery::BakeRooms(Asset, BakeReport, bForceFullRebake);

	// 5. Levels, then back into the map.
	FMazeExportReport ExportReport;
	FMazeLevelExporter::ExportRooms(Asset, ExportReport);

	const int32 Attached = FMazeLevelAttacher::AttachRooms(Asset);

	RefreshStatus();
	OnSettingsChanged.Broadcast();

	UE_LOG(LogMazeForge, Log,
		TEXT("Apply changes: %d rooms, %d rebuilt, %d left alone, %d levels attached; "
		     "in %.2f s. Save everything — Ctrl+Shift+S."),
		Asset->Rooms.Num(), BakeReport.Rooms - BakeReport.SkippedRooms, BakeReport.SkippedRooms,
		Attached, FPlatformTime::Seconds() - StartTime);
}

// -------------------------------------------------------------------- 1. generate

void UMazeEdModeSettings::GenerateMaze()
{
	if (UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Generate")))
	{
		Asset->RunGenerator();
		RefreshStatus();
	}
}

void UMazeEdModeSettings::ClearMaze()
{
	UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Clear"));
	if (!Asset)
	{
		return;
	}

	// Everything the maze currently is, in the order that leaves nothing pointing at something
	// that no longer exists.
	//
	// 1. The room levels come out of the map first. Clearing the grid without this would leave
	//    their geometry standing in the world with no room, no slicing and no asset behind it —
	//    visible, selectable, and impossible to get rid of from this panel afterwards.
	const int32 Detached = FMazeLevelAttacher::DetachRooms(Asset);

	// 2. The grid, the slicing and the bake hashes. The asset snapshots itself first, so Restore
	//    brings the drawing back — but only the drawing: the levels are not part of a snapshot.
	Asset->ClearGrid();

	// 3. The manifest reference. The manifest describes rooms that are gone; leaving the field
	//    set would have the asset claim a build it no longer has, and the runtime would load it.
	Asset->Modify();
	Asset->Manifest = nullptr;

	RefreshStatus();
	OnSettingsChanged.Broadcast();

	// Said plainly, because this is the one button whose damage a snapshot does not cover.
	UE_LOG(LogMazeForge, Log,
		TEXT("Clear all changes: the drawing, the slicing and the bake state are gone, "
		     "%d levels detached, the manifest reference cleared. Restore brings the drawing "
		     "back — not the levels; those come back from Apply Changes. The level and mesh "
		     "assets are left on disk and are overwritten by the next build."),
		Detached);
}

// ----------------------------------------------------------------------- 2. shape

void UMazeEdModeSettings::OpenWorldMap()
{
	FGlobalTabmanager::Get()->TryInvokeTab(SMazeWorldMap::TabId);
}

void UMazeEdModeSettings::BuildDepthVolume()
{
	if (UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Volume")))
	{
		Asset->BuildDepthVolume();
		RefreshStatus();
	}
}

void UMazeEdModeSettings::FlattenToPlane()
{
	if (UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Plane")))
	{
		Asset->FlattenToPlane();
		RefreshStatus();
	}
}

void UMazeEdModeSettings::FillBackWall()
{
	if (UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Back wall")))
	{
		Asset->FillBackWall(static_cast<uint8>(FMath::Clamp(PaintVariant, 0, 255)));
		RefreshStatus();
	}
}

void UMazeEdModeSettings::ClearBackWall()
{
	if (UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Back wall")))
	{
		Asset->ClearBackWall();
		RefreshStatus();
	}
}

void UMazeEdModeSettings::SaveSnapshot()
{
	if (UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Snapshot")))
	{
		Asset->SaveSnapshot();
		RefreshStatus();
	}
}

void UMazeEdModeSettings::RestoreSnapshot()
{
	if (UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Snapshot")))
	{
		Asset->RestoreSnapshot();
		RefreshStatus();
	}
}

// ----------------------------------------------------------------------- 3. rooms

void UMazeEdModeSettings::SliceIntoRooms()
{
	if (UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Slicing")))
	{
		Asset->SliceIntoRooms();
		RefreshStatus();
	}
}

// ---------------------------------------------------------------------- 4. meshes

void UMazeEdModeSettings::BakeRoomMeshes()
{
	UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Mesh bake"));
	if (!Asset)
	{
		return;
	}

	FMazeBakeReport Report;
	FMazeBakery::BakeRooms(Asset, Report, bForceFullRebake);
	RefreshStatus();
}

// ---------------------------------------------------------------------- 5. levels

void UMazeEdModeSettings::ExportRoomsToLevels()
{
	UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Export"));
	if (!Asset)
	{
		return;
	}

	FMazeExportReport Report;
	FMazeLevelExporter::ExportRooms(Asset, Report);
	RefreshStatus();
}

void UMazeEdModeSettings::AttachRoomsToLevel()
{
	if (UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Attach")))
	{
		FMazeLevelAttacher::AttachRooms(Asset);

		// These two are the only buttons that change nothing in the grid, so the preview hears
		// nothing from the asset — and the preview is exactly what has to change, because it
		// stops drawing rooms whose real geometry is now in the world.
		RefreshStatus();
		OnSettingsChanged.Broadcast();
	}
}

void UMazeEdModeSettings::AttachAllMazes()
{
	UMazeWorldGraph* Graph = WorldGraph.LoadSynchronous();
	if (!Graph)
	{
		// Said out loud rather than silently doing nothing: an empty field and a graph that
		// lists no mazes look identical from the button, and the fix differs for each.
		UE_LOG(LogMazeForge, Warning,
			TEXT("Attach world: the World Graph field is empty or points at an asset that is "
			     "gone (%s). Pick the same graph the character's streaming component uses."),
			*WorldGraph.ToString());
		return;
	}

	FMazeLevelAttacher::AttachWorld(Graph);

	// Same reason as Attach Rooms To Level: nothing in the grid changed, so the preview hears
	// nothing from the asset, and the preview is exactly what has to change.
	RefreshStatus();
	OnSettingsChanged.Broadcast();
}

void UMazeEdModeSettings::DetachRoomsFromLevel()
{
	if (UMazeGridAsset* Asset = RequireTarget(TargetAsset.LoadSynchronous(), TEXT("Detach")))
	{
		FMazeLevelAttacher::DetachRooms(Asset);

		RefreshStatus();
		OnSettingsChanged.Broadcast();
	}
}
