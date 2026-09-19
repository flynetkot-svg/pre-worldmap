#include "Mode/MazeEdMode.h"

#include "Assets/MazeGridAsset.h"
#include "Assets/MazeObjectLibrary.h"
#include "Assets/MazeSpawnAsset.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Data/MazeGrid.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "Assets/MazeBuildSettings.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "Export/MazeExportUtils.h"
#include "Export/MazeLevelAttacher.h"
#include "MazeEdModeToolkit.h"
#include "MazeForgeCore.h"
#include "Mode/MazeEdModeSettings.h"
#include "Render/MazeEditorStyleAsset.h"
#include "Render/MazeGridRenderer.h"
#include "Render/MazePreviewActor.h"
#include "Slicers/MazeRoomSlicerBase.h"
#include "Spawner/MazePlacementRules.h"
#include "SceneView.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "MazeForgeEditor"

const FEditorModeID UMazeEdMode::EM_MazeForge = TEXT("EM_MazeForge");

// The cursor colours moved into the style preset (UMazeEditorStyleAsset): tuning a readable
// overlay for a particular maze material by rebuilding the plugin is a poor workflow.

UMazeEdMode::UMazeEdMode()
{
	Info = FEditorModeInfo(
		EM_MazeForge,
		LOCTEXT("MazeEdModeName", "MazeForge"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Brush"),
		true);
}

// -------------------------------------------------------------------- life cycle

void UMazeEdMode::Initialize()
{
	// The settings must exist BEFORE Super::Enter(): UEdMode::Enter creates the toolkit, and
	// while building the panel the toolkit reads GetSettings() straight away.
	// If we created them afterwards, Details would get a nullptr and the panel would stay empty.
	if (!Settings)
	{
		Settings = NewObject<UMazeEdModeSettings>(this, NAME_None, RF_Transactional);

		// The settings object is never saved anywhere in the project — it only lives while the
		// editor is open. The chosen target and preset are pulled back from the ini, otherwise
		// every launch would start with an empty panel.
		Settings->LoadConfig();
	}

	Super::Initialize();
}

void UMazeEdMode::Enter()
{
	if (!Settings)
	{
		Settings = NewObject<UMazeEdModeSettings>(this, NAME_None, RF_Transactional);

		// The settings object is never saved anywhere in the project — it only lives while the
		// editor is open. The chosen target and preset are pulled back from the ini, otherwise
		// every launch would start with an empty panel.
		Settings->LoadConfig();
	}

	Super::Enter();

	Settings->OnSettingsChanged.AddUObject(this, &UMazeEdMode::OnSettingsChanged);

	// Levels are not only added and removed by our own buttons — the Levels panel does it too,
	// and the preview has to agree with what is actually in the world either way.
	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(
		this, &UMazeEdMode::OnLevelChangedInWorld);
	LevelRemovedHandle = FWorldDelegates::LevelRemovedFromWorld.AddUObject(
		this, &UMazeEdMode::OnLevelChangedInWorld);

	EnsurePreviewActor();

	// A stroke belongs to one entry into the mode. Inheriting one is what made the tool look
	// broken: see AbortStroke.
	bPainting = false;
	bErasing = false;
	bBoxDrag = false;
	bPeeking = false;
	StrokeChangedCells = 0;

	BindAsset(GetTargetAsset());
	RebuildPreview();

	// The panel opens already filled in: the target may have carried over from the previous
	// entry into the mode, and an empty status line would be confusing.
	Settings->RefreshStatus();
}

void UMazeEdMode::Exit()
{
	if (Settings)
	{
		Settings->OnSettingsChanged.RemoveAll(this);
	}

	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
	FWorldDelegates::LevelRemovedFromWorld.Remove(LevelRemovedHandle);
	LevelAddedHandle.Reset();
	LevelRemovedHandle.Reset();

	// Leaving the mode mid-stroke would orphan an open transaction, which is worse than a stuck
	// flag: the editor keeps collecting every later change into it.
	AbortStroke();

	BindAsset(nullptr);

	bPeeking = false;

	if (Preview)
	{
		Preview->Destroy();
		Preview = nullptr;
	}

	Super::Exit();
}

void UMazeEdMode::CreateToolkit()
{
	Toolkit = MakeShared<FMazeEdModeToolkit>();
}

UMazeGridAsset* UMazeEdMode::GetTargetAsset() const
{
	return Settings ? Settings->TargetAsset.LoadSynchronous() : nullptr;
}

void UMazeEdMode::BindAsset(UMazeGridAsset* Asset)
{
	if (UMazeGridAsset* Previous = BoundAsset.Get())
	{
		if (GridChangedHandle.IsValid())
		{
			Previous->OnGridChanged.Remove(GridChangedHandle);
		}
	}

	GridChangedHandle.Reset();
	BoundAsset = Asset;

	if (Asset)
	{
		// The Run Generator / Slice buttons in Details change the grid behind the mode's back,
		// so we listen to the asset.
		GridChangedHandle = Asset->OnGridChanged.AddUObject(this, &UMazeEdMode::OnAssetGridChanged);
	}
}

void UMazeEdMode::OnLevelChangedInWorld(ULevel* Level, UWorld* World)
{
	// Only our own world: a level opening somewhere else — a preview scene, another editor world —
	// has nothing to do with what this mode draws.
	if (World != GetWorld())
	{
		return;
	}

	if (Settings)
	{
		Settings->RefreshStatus();
	}

	RebuildPreview();
}

void UMazeEdMode::OnAssetGridChanged()
{
	RebuildPreview();
}

void UMazeEdMode::OnSettingsChanged()
{
	if (BoundAsset.Get() != GetTargetAsset())
	{
		// Switching the target takes the previous maze off the map.
		//
		// Only the detach is automatic, and that asymmetry is the point. Detaching is fast, it
		// loads nothing, and it removes the one state that is genuinely confusing: another maze's
		// real geometry standing behind this maze's preview cubes. Attaching is the opposite —
		// it loads every room level, and a dropdown that thinks for ten seconds is a bad dropdown.
		// So the new maze arrives on the map when it is asked for, which Apply Changes does anyway
		// as its last step.
		//
		// This runs here and not in BindAsset because BindAsset is also how Enter and Exit set up
		// and tear down. Leaving the mode must not unload the designer's levels.
		// Off is for testing a door between two mazes in PIE, where both have to be attached at
		// once and this would take the first one away again. See the setting's comment.
		if (UMazeGridAsset* Previous = Settings && Settings->bDetachPreviousOnTargetSwitch
			? BoundAsset.Get() : nullptr)
		{
			const int32 Detached = FMazeLevelAttacher::DetachRooms(Previous);
			if (Detached > 0)
			{
				UE_LOG(LogMazeForge, Log,
					TEXT("Target changed to %s: %d levels of %s taken off the map. "
					     "Press Apply Changes to put the new one there."),
					*GetNameSafe(GetTargetAsset()), Detached, *Previous->GetName());
			}
		}

		BindAsset(GetTargetAsset());

		// The two counters describe the maze that just left. Reset, or the first rebuild of the
		// new one compares against numbers from a different maze and stays quiet about them.
		LastSuppressedRoomCount = -1;
		LastPreviewInstanceCount = -1;
	}

	if (UMazeGridAsset* Asset = GetTargetAsset())
	{
		Settings->ActiveDepthSlice =
			FMath::Clamp(Settings->ActiveDepthSlice, 0, FMath::Max(0, Asset->Grid.DepthCells() - 1));
	}

	RebuildPreview();
}

void UMazeEdMode::EnsurePreviewActor()
{
	if (IsValid(Preview) && !Preview->IsActorBeingDestroyed())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;
	SpawnParams.bHideFromSceneOutliner = true;

	// The persistent level, explicitly, and this is the whole bug. Without an override the actor
	// goes into the world's CURRENT level — and Attach Rooms To Level makes a room level current.
	// Enter the mode after an attach and the preview is born inside a room; the next Detach takes
	// that room out of the world and the preview dies with it. The grid, the slicing and the log
	// all stay perfectly correct, and the viewport is empty. Only restarting the editor helped,
	// because that is what put a fresh actor in a fresh persistent level.
	SpawnParams.OverrideLevel = World->PersistentLevel;

	Preview = World->SpawnActor<AMazePreviewActor>(
		AMazePreviewActor::StaticClass(), FTransform::Identity, SpawnParams);
}

void UMazeEdMode::RebuildRoomPreview()
{
	PreviewRooms.Reset();

	if (!Settings || !Settings->bShowRoomPreview)
	{
		return;
	}

	const UMazeGridAsset* Asset = GetTargetAsset();
	if (!Asset || !Asset->Slicer || Asset->Grid.NumCells() == 0)
	{
		return;
	}

	// Execute is the same call Slice Into Rooms makes, and it writes nothing: it fills the array
	// it is handed. That is what makes an honest preview possible — the answer shown is produced
	// by the code that will produce the real one, not by a second implementation that agrees with
	// it until someone changes one of them.
	Asset->Slicer->Execute(Asset->Grid, PreviewRooms);
}

void UMazeEdMode::GatherExportedRoomIds(TSet<FName>& OutRoomIds) const
{
	const UMazeGridAsset* Asset = GetTargetAsset();
	const UWorld* World = GetWorld();
	if (!Asset || !World)
	{
		return;
	}

	const UMazeBuildSettings* BuildSettings = Asset->BuildSettings.LoadSynchronous();
	const FString MazeName = Asset->GetSafeMazeName();
	const FString LevelRoot = MazeExport::ScopedRoot(
		BuildSettings ? BuildSettings->LevelPackageRoot : FString(TEXT("/Game/MazeForge/Maps")),
		MazeName);

	// A room level attached to the persistent map already shows that room's real geometry.
	// Drawing the preview on top of it would put two identical surfaces in the same place
	// and cause depth fighting.
	// GetLoadedLevel() is not the question. The question is whether the level is still part of
	// the world, and those two answers disagree for a while after a level is detached.
	//
	// This is what blanked the preview. UEditorLevelUtils::RemoveLevelFromWorld takes the level
	// out of World->Levels and reports success, but the ULevelStreaming entry survives in
	// GetStreamingLevels() with GetLoadedLevel() still returning the orphaned level until the
	// next garbage collection. So the rebuild that ran immediately after Change Current Maze
	// still believed every room was on screen as real geometry, skipped every cell inside those
	// rooms — and with two rooms covering the whole maze, that is every cell there is. The
	// backdrop and the world borders are suppressed by the same flag, so the viewport went
	// completely empty, painting appeared to do nothing, and it all came back the moment
	// anything triggered another rebuild a few seconds later.
	//
	// World->GetLevels() is the authoritative membership list and is updated by the removal
	// itself, so it cannot lag behind it.
	const TArray<ULevel*>& LevelsInWorld = World->GetLevels();

	TSet<FString> LoadedPackages;
	for (const ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (!Streaming)
		{
			continue;
		}

		ULevel* Loaded = Streaming->GetLoadedLevel();
		if (Loaded && LevelsInWorld.Contains(Loaded))
		{
			LoadedPackages.Add(Streaming->GetWorldAssetPackageFName().ToString());
		}
	}

	if (LoadedPackages.Num() == 0)
	{
		return;
	}

	for (const FMazeRoomDesc& Room : Asset->Rooms)
	{
		const FString Expected = LevelRoot / MazeExport::LevelAssetName(MazeName, Room.RoomId);
		if (LoadedPackages.Contains(Expected))
		{
			OutRoomIds.Add(Room.RoomId);
		}
	}
}

void UMazeEdMode::RebuildPreview()
{
	// Checked on every rebuild rather than only on entering the mode: the actor can be taken out
	// of the world by a level being removed, and that happens on an ordinary button press.
	EnsurePreviewActor();

	if (!IsValid(Preview))
	{
		return;
	}

	const bool bShow = Settings && Settings->bShowPreview;
	if (!bShow)
	{
		Preview->Rebuild(nullptr);
		InvalidateViewports();
		return;
	}

	RebuildRoomPreview();

	TSet<FName> ExportedRoomIds;
	GatherExportedRoomIds(ExportedRoomIds);

	// Rooms hidden behind their own level geometry are the one thing that can empty the preview
	// while the grid is perfectly intact, so it is reported — but only when the number changes,
	// or a stroke would fill the log with it.
	if (ExportedRoomIds.Num() != LastSuppressedRoomCount)
	{
		LastSuppressedRoomCount = ExportedRoomIds.Num();

		const UMazeGridAsset* Asset = GetTargetAsset();
		UE_LOG(LogMazeForge, Log,
			TEXT("Preview: %d of %d rooms are hidden — their levels are in the map and already "
			     "show the real geometry."),
			LastSuppressedRoomCount, Asset ? Asset->Rooms.Num() : 0);
	}

	// Only the synchronous tree build is skipped mid-stroke: there the mouse invalidates the
	// viewport on every move anyway, and forcing the build on each step would be paid for nothing.
	Preview->Rebuild(GetTargetAsset(), ExportedRoomIds,
		Settings->PaintType, Settings->bIsolateActiveLayer && !bPeeking, !bPainting,
		Settings->InactiveLayerDim);

	// Asking for a frame is not skipped, ever. It was, and that tied "the display does not
	// update" to a stroke flag that could get stuck — one bug hiding behind another.
	InvalidateViewports();

	// The number that separates "the preview built nothing" from "it built everything and you
	// cannot see it". Logged only when it changes, so a stroke does not fill the log with it.
	const UMazeGridAsset* Asset = GetTargetAsset();
	const int32 Drawn = Asset ? Asset->Grid.NumCells() : 0;
	if (Drawn != LastPreviewInstanceCount)
	{
		LastPreviewInstanceCount = Drawn;
		UE_LOG(LogMazeForge, Log, TEXT("Preview: %d cells handed to the preview actor."), Drawn);
	}
}

void UMazeEdMode::InvalidateViewports()
{
	// The preview is built from components, and changing a component does not by itself make the
	// editor draw a new frame. Every Invalidate in this file used to sit inside a mouse or key
	// handler, so drawing worked and buttons did not: press one and the viewport kept the frame
	// it already had, until switching modes or touching a property in the Details panel happened
	// to force a redraw for its own reasons. That is the whole of the "the maze disappeared after
	// Detach / Change Current Maze" bug.
	if (!GEditor)
	{
		return;
	}

	for (FEditorViewportClient* Client : GEditor->GetAllViewportClients())
	{
		if (Client)
		{
			Client->Invalidate(false, false);
		}
	}
}

// ---------------------------------------------------------------- rendering

void UMazeEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	Super::Render(View, Viewport, PDI);

	const UMazeGridAsset* Asset = GetTargetAsset();
	if (!Asset || !Settings)
	{
		return;
	}

	const FMazeGrid& Grid = Asset->Grid;
	const UMazeEditorStyleAsset& Style = Settings->GetStyle();

	FIntVector Centre(Grid.SizeXZ.X / 2, Settings->ActiveDepthSlice, Grid.SizeXZ.Y / 2);
	if (bHasHover)
	{
		Centre = HoveredCell;
	}
	else
	{
		// With no cursor we draw the grid overlay around whatever the camera is looking at.
		// The window used to be built around the centre of the maze — on a 256x128 map that is
		// 128 m off to the side, and the grid overlay simply was not visible.
		ProjectViewCentreToCell(View, Grid, Centre);
	}

	if (Settings->bShowGrid)
	{
		FIntPoint MinXZ;
		FIntPoint MaxXZ;

		if (!ComputeVisibleCellRange(View, Grid, MinXZ, MaxXZ))
		{
			// Fallback for views where the slice plane is not crossed by the frame.
			const int32 Half = FMath::Max(4, Settings->GridWindowCells / 2);
			MinXZ = FIntPoint(FMath::Clamp(Centre.X - Half, 0, Grid.SizeXZ.X),
			                  FMath::Clamp(Centre.Z - Half, 0, Grid.SizeXZ.Y));
			MaxXZ = FIntPoint(FMath::Clamp(Centre.X + Half, 0, Grid.SizeXZ.X),
			                  FMath::Clamp(Centre.Z + Half, 0, Grid.SizeXZ.Y));
		}

		const int32 Step = FMazeGridRenderer::ChooseStep(MinXZ, MaxXZ, Style.MaxGridLines);
		FMazeGridRenderer::DrawGrid(PDI, Grid, Settings->ActiveDepthSlice, MinXZ, MaxXZ, Step, Style);
		FMazeGridRenderer::DrawDepthBands(PDI, Grid, MinXZ, MaxXZ, Style);

		// The maze bounds go last, on top of the grid overlay: they matter more than fine lines.
		FMazeGridRenderer::DrawMazeBounds(PDI, Grid, Settings->ActiveDepthSlice, Style);
	}

	if (Settings->bShowRooms)
	{
		// The room under the cursor is highlighted more brightly — you can see at a glance which
		// level whatever you are drawing right now will end up in.
		FName HoveredRoomId;
		if (bHasHover)
		{
			// HoveredCell is a grid cell, so its room coordinates are X and Z — not X and Y.
			if (const FMazeRoomDesc* Room =
				MazeRooms::FindAtXZ(Asset->Rooms, FIntPoint(HoveredCell.X, HoveredCell.Z)))
			{
				HoveredRoomId = Room->RoomId;
			}
		}

		FIntPoint RoomsMinXZ(0, 0);
		FIntPoint RoomsMaxXZ(Grid.SizeXZ.X, Grid.SizeXZ.Y);
		ComputeVisibleCellRange(View, Grid, RoomsMinXZ, RoomsMaxXZ);

		// Either the slicing that exists, or the one that would exist — never both, or every
		// seam would be drawn twice and the two would be impossible to tell apart.
		const TArray<FMazeRoomDesc>& RoomsToDraw =
			Settings->bShowRoomPreview ? PreviewRooms : Asset->Rooms;

		FMazeGridRenderer::DrawRooms(PDI, Grid, RoomsToDraw, RoomsMinXZ, RoomsMaxXZ,
			HoveredRoomId, Settings->ActiveDepthSlice, Style);
	}

	// The placed objects are always drawn, whichever brush is in hand: they are part of the
	// level, and hiding them while the cell brush is selected would mean painting mass into a
	// spot already occupied without ever seeing it.
	if (const UMazeSpawnAsset* Spawns = Settings->GetSpawnAsset())
	{
		if (Spawns->Placements.Num() > 0)
		{
			FMazeGridRenderer::DrawPlacements(PDI, Grid, Settings->GetObjectLibrary(),
				Spawns->Placements, Style);
		}
	}

	if (!bHasHover)
	{
		return;
	}

	if (bBoxDrag)
	{
		const FIntVector Min(
			FMath::Min(DragStartCell.X, HoveredCell.X), Settings->ActiveDepthSlice,
			FMath::Min(DragStartCell.Z, HoveredCell.Z));
		const FIntVector Max(
			FMath::Max(DragStartCell.X, HoveredCell.X), Settings->ActiveDepthSlice,
			FMath::Max(DragStartCell.Z, HoveredCell.Z));

		FMazeGridRenderer::DrawCellHighlight(PDI, Grid, Min, Max,
			bErasing ? Style.BrushErase : Style.BoxDrag);
	}
	else if (Settings->Tool == EMazeEditTool::Objects)
	{
		// The footprint rectangle, in the colour of what would happen: green it fits, red the
		// geometry refuses, amber it lands on something already there. Seeing the rectangle
		// before the click is the whole answer to "where exactly does a 2x3 wardrobe land when
		// I aim at this cell" — and the colour is where the verdict belongs, because the eye is
		// here and not at the end of the status bar.
		const UMazeObjectLibrary* Library = Settings->GetObjectLibrary();
		const FMazeObjectType* Type = Library
			? Library->FindType(Settings->PaintObjectType)
			: nullptr;

		const FIntPoint Size = Type
			? FIntPoint(FMath::Max(1, Type->FootprintCells.X), FMath::Max(1, Type->FootprintCells.Y))
			: FIntPoint(1, 1);

		const int32 SliceY = MazePlacement::BandSliceY(Grid, Settings->PaintBand);
		const FIntPoint CellXZ(HoveredCell.X, HoveredCell.Z);

		EMazeAnchorKind Anchor = EMazeAnchorKind::Floor;
		const bool bFits = Type
			&& MazePlacement::FindAnchor(Grid, *Type, CellXZ, SliceY, Anchor);

		// Asked only when the geometry has already said yes: "it will not fit AND something is
		// in the way" is one piece of news, not two, and the refusal is the half worth showing.
		const UMazeSpawnAsset* Spawns = Settings->GetSpawnAsset();
		const bool bBlocked = bFits && Spawns
			&& Spawns->FindOverlapping(Library, CellXZ, Size) != INDEX_NONE;

		const FIntVector Min(CellXZ.X, SliceY, CellXZ.Y);
		const FIntVector Max(CellXZ.X + Size.X - 1, SliceY, CellXZ.Y + Size.Y - 1);

		FMazeGridRenderer::DrawCellHighlight(PDI, Grid, Min, Max,
			!bFits ? Style.BrushErase : (bBlocked ? Style.BrushBlocked : Style.BrushPaint));
	}
	else
	{
		const int32 Half = FMath::Max(0, Settings->BrushSize - 1) / 2;
		const FIntVector Min(HoveredCell.X - Half, Settings->ActiveDepthSlice, HoveredCell.Z - Half);
		const FIntVector Max(HoveredCell.X + Half, Settings->ActiveDepthSlice, HoveredCell.Z + Half);

		FMazeGridRenderer::DrawCellHighlight(PDI, Grid, Min, Max,
			bErasing ? Style.BrushErase : Style.BrushPaint);
	}
}

void UMazeEdMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport,
                          const FSceneView* View, FCanvas* Canvas)
{
	Super::DrawHUD(ViewportClient, Viewport, View, Canvas);

	if (!Settings)
	{
		return;
	}

	const UMazeGridAsset* Asset = GetTargetAsset();

	FString Status;
	if (!Asset)
	{
		Status = TEXT("MazeForge: pick a Target Asset in the mode panel");
	}
	else
	{
		const FMazeGrid& Grid = Asset->Grid;
		const EMazeDepthBand Band = Grid.Depth.GetBand(Settings->ActiveDepthSlice);
		const TCHAR* BandName =
			Band == EMazeDepthBand::Play ? TEXT("PLAY") :
			Band == EMazeDepthBand::Background ? TEXT("BACKGROUND") : TEXT("FOREGROUND");

		Status = FString::Printf(
			TEXT("MazeForge  |  slice Y %d/%d (%s)  |  brush %d  |  cells %d  |  rooms %d"),
			Settings->ActiveDepthSlice, Grid.DepthCells() - 1, BandName,
			Settings->BrushSize, Grid.NumCells(), Asset->Rooms.Num());

		// The slice indicator above belongs to the CELL brush. The object brush does not use it
		// at all — it uses Paint Band — and the two disagreeing in silence is what put eight
		// gates into the background while the frame said PLAY in capital letters.
		if (Settings->Tool == EMazeEditTool::Objects)
		{
			const EMazeDepthBand ObjectBand = Settings->PaintBand;

			if (ObjectBand == EMazeDepthBand::Play)
			{
				Status += TEXT("  |  objects -> PLAY");
			}
			else
			{
				Status += FString::Printf(
					TEXT("  |  objects -> %s — THE PLAYER NEVER GOES THERE"),
					ObjectBand == EMazeDepthBand::Background ? TEXT("BACKGROUND") : TEXT("FOREGROUND"));
			}
		}

		// An empty Foreground is the default and it is deliberate — it is the cutaway of the
		// original, and the designer's workspace for near-plane decor. But "there is nothing
		// here" and "nothing is ever built here" look identical in the frame, and the second one
		// costs a bug report every time. So the frame says which.
		if (!Grid.Depth.IsFillBand(Band))
		{
			const TCHAR* FillName =
				Band == EMazeDepthBand::Play ? TEXT("Fill Play") :
				Band == EMazeDepthBand::Background ? TEXT("Fill Background") : TEXT("Fill Foreground");

			Status += FString::Printf(
				TEXT("  |  BAND NOT BUILT — tick %s in the depth profile"), FillName);
		}

		// "The room slicing disappeared" almost always means there simply are no rooms:
		// Clear Maze and Restore Snapshot reset them, because the old slicing has nothing to do
		// with the new grid. Let that be visible right there in the frame.
		if (Settings->bShowRooms && Asset->Rooms.Num() == 0)
		{
			Status += TEXT("  |  NO SLICING — press Slice Into Rooms");
		}

		// Worth its own word in the frame: while peeking, what you see is not the layer order you
		// paint in, and a stroke made now would land somewhere other than where it looks.
		if (bPeeking)
		{
			Status += TEXT("  |  PEEK");
		}

		if (bHasHover)
		{
			Status += FString::Printf(TEXT("  |  cursor  X %d  Z %d"), HoveredCell.X, HoveredCell.Z);

			// The object brush is allowed to place something that does not fit — the hand is
			// allowed to be wrong. What it is not allowed to do is stay quiet about it until the
			// export, twenty minutes and eight clicks later. So the verdict goes in the frame,
			// under the cursor, BEFORE the click.
			if (Settings->Tool == EMazeEditTool::Objects)
			{
				Status += ObjectHoverStatus(*Asset);
			}
		}
		else if (bViewParallelToPlane)
		{
			Status += TEXT("  |  VIEW LOOKS ALONG THE DRAWING PLANE — switch to Left/Right or perspective");
		}
		else if (!NoHoverReason.IsEmpty())
		{
			Status += FString::Printf(TEXT("  |  NO CURSOR: %s"), *NoHoverReason);
		}
	}

	FCanvasTextItem TextItem(FVector2D(12.0f, 12.0f), FText::FromString(Status),
		GEngine->GetSmallFont(), FLinearColor::White);
	TextItem.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(TextItem);
}

// ---------------------------------------------------------------- cursor

bool UMazeEdMode::ComputeVisibleCellRange(const FSceneView* View, const FMazeGrid& Grid,
                                          FIntPoint& OutMinXZ, FIntPoint& OutMaxXZ) const
{
	if (!View || !Settings)
	{
		return false;
	}

	const FIntRect Rect = View->UnscaledViewRect;
	if (Rect.Width() <= 0 || Rect.Height() <= 0)
	{
		return false;
	}

	const double PlaneY = Grid.CellToWorld(FIntVector(0, Settings->ActiveDepthSlice, 0)).Y;

	const FVector2D Corners[4] = {
		FVector2D(Rect.Min.X, Rect.Min.Y),
		FVector2D(Rect.Max.X, Rect.Min.Y),
		FVector2D(Rect.Min.X, Rect.Max.Y),
		FVector2D(Rect.Max.X, Rect.Max.Y)
	};

	FBox2D PlaneBounds(ForceInit);

	for (const FVector2D& Corner : Corners)
	{
		FVector RayOrigin;
		FVector RayDirection;
		View->DeprojectFVector2D(Corner, RayOrigin, RayDirection);

		if (FMath::Abs(RayDirection.Y) < 0.05)
		{
			return false;
		}

		const double T = (PlaneY - RayOrigin.Y) / RayDirection.Y;
		if (T <= 0.0)
		{
			// The slice plane is behind the camera — there is nothing to draw.
			return false;
		}

		const FVector HitPoint = RayOrigin + RayDirection * T;
		PlaneBounds += FVector2D(HitPoint.X, HitPoint.Z);
	}

	// World -> cells, with one cell of margin, then clipped to the maze bounds.
	const int32 MinX = FMath::FloorToInt32((PlaneBounds.Min.X - Grid.WorldOrigin.X) / Grid.CellSize.X) - 1;
	const int32 MaxX = FMath::CeilToInt32((PlaneBounds.Max.X - Grid.WorldOrigin.X) / Grid.CellSize.X) + 1;
	const int32 MinZ = FMath::FloorToInt32((PlaneBounds.Min.Y - Grid.WorldOrigin.Z) / Grid.CellSize.Z) - 1;
	const int32 MaxZ = FMath::CeilToInt32((PlaneBounds.Max.Y - Grid.WorldOrigin.Z) / Grid.CellSize.Z) + 1;

	OutMinXZ = FIntPoint(FMath::Clamp(MinX, 0, Grid.SizeXZ.X), FMath::Clamp(MinZ, 0, Grid.SizeXZ.Y));
	OutMaxXZ = FIntPoint(FMath::Clamp(MaxX, 0, Grid.SizeXZ.X), FMath::Clamp(MaxZ, 0, Grid.SizeXZ.Y));

	return OutMaxXZ.X > OutMinXZ.X && OutMaxXZ.Y > OutMinXZ.Y;
}

bool UMazeEdMode::ProjectViewCentreToCell(const FSceneView* View, const FMazeGrid& Grid, FIntVector& OutCell) const
{
	if (!View || !Settings)
	{
		return false;
	}

	const FVector ViewOrigin = View->ViewMatrices.GetViewOrigin();
	const FVector ViewDirection = View->GetViewDirection();

	if (FMath::Abs(ViewDirection.Y) < UE_SMALL_NUMBER)
	{
		return false;
	}

	const double PlaneY = Grid.CellToWorld(FIntVector(0, Settings->ActiveDepthSlice, 0)).Y;
	const double T = (PlaneY - ViewOrigin.Y) / ViewDirection.Y;
	const FVector HitPoint = ViewOrigin + ViewDirection * T;

	FIntVector Cell = Grid.WorldToCell(HitPoint);

	// A view aimed past the maze is not discarded but clamped to the nearest edge:
	// that way the designer sees the boundary of the grid overlay and knows where to fly.
	Cell.X = FMath::Clamp(Cell.X, 0, FMath::Max(0, Grid.SizeXZ.X - 1));
	Cell.Z = FMath::Clamp(Cell.Z, 0, FMath::Max(0, Grid.SizeXZ.Y - 1));
	Cell.Y = Settings->ActiveDepthSlice;

	OutCell = Cell;
	return true;
}

bool UMazeEdMode::ComputeCellUnderCursor(FEditorViewportClient* ViewportClient, FIntVector& OutCell)
{
	const UMazeGridAsset* Asset = GetTargetAsset();
	if (!Asset || !ViewportClient || !Settings)
	{
		return false;
	}

	const FMazeGrid& Grid = Asset->Grid;

	const FViewportCursorLocation Cursor = ViewportClient->GetCursorWorldLocationFromMousePos();
	const FVector Origin = Cursor.GetOrigin();
	const FVector Direction = Cursor.GetDirection();

	// We catch the cursor on the plane of the active depth slice.
	const double PlaneY = Grid.CellToWorld(FIntVector(0, Settings->ActiveDepthSlice, 0)).Y;

	// A view along the drawing plane (in UE that is Front/Back and Top/Bottom):
	// the ray is parallel to it, so there is no intersection. We remember this so we can say it
	// in the HUD instead of staying silent — otherwise it looks as if the tool is broken.
	bViewParallelToPlane = FMath::Abs(Direction.Y) < 0.05;
	if (bViewParallelToPlane)
	{
		NoHoverReason = TEXT("the view looks along the drawing plane");
		return false;
	}

	const double T = (PlaneY - Origin.Y) / Direction.Y;

	// "Behind the camera" only means something in a perspective view.
	//
	// An orthographic viewport draws the whole scene along its axis regardless of where its camera
	// happens to sit on that axis — UE puts the ortho near plane at -HALF_WORLD_MAX precisely so
	// that it does. So an ortho camera whose Y has drifted inside the maze still shows every slice,
	// front and back, and picking a slice "behind" it is perfectly well defined.
	//
	// Requiring T > 0 there rejected hits that were plainly visible on screen. It only bit after
	// the ortho camera's Y moved into the depth volume, which is why it looked like a sudden
	// regression: with the camera at Y between the background and the play plane, the background
	// slice still picked and the play slice did not, and the brush went dead in the one slice that
	// matters. Nothing about the grid or the brush was involved.
	if (!ViewportClient->IsOrtho() && T <= 0.0)
	{
		NoHoverReason = TEXT("the drawing plane is behind the camera — turn the view around");
		return false;
	}

	const FVector HitPoint = Origin + Direction * T;

	FIntVector Cell = Grid.WorldToCell(HitPoint);
	Cell.Y = Settings->ActiveDepthSlice;

	if (!Grid.IsInside(Cell))
	{
		NoHoverReason = FString::Printf(
			TEXT("outside the grid: X %d Z %d, and the grid is %d x %d"),
			Cell.X, Cell.Z, Grid.SizeXZ.X, Grid.SizeXZ.Y);
		return false;
	}

	NoHoverReason.Reset();
	OutCell = Cell;
	return true;
}

void UMazeEdMode::ReconcilePeek(FViewport* Viewport)
{
	if (!bPeeking || !Settings)
	{
		return;
	}

	if (Viewport && Viewport->KeyState(Settings->PeekKey))
	{
		return;
	}

	bPeeking = false;
	RebuildPreview();
}

bool UMazeEdMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 X, int32 Y)
{
	ReconcilePeek(Viewport);

	bHasHover = ComputeCellUnderCursor(ViewportClient, HoveredCell);

	if (bPainting)
	{
		ContinueStroke();
	}

	if (ViewportClient)
	{
		ViewportClient->Invalidate(false, false);
	}

	return false;
}

bool UMazeEdMode::CapturedMouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 X, int32 Y)
{
	if (!bPainting)
	{
		return false;
	}

	bHasHover = ComputeCellUnderCursor(ViewportClient, HoveredCell);
	ContinueStroke();

	if (ViewportClient)
	{
		ViewportClient->Invalidate(false, false);
	}

	return true;
}

// ---------------------------------------------------------------- input

bool UMazeEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (!Settings || !Viewport)
	{
		return false;
	}

	const bool bAlt = Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt);
	const bool bShift = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
	const bool bCtrl = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);

	if (Event == IE_Pressed)
	{
		if (Key == EKeys::PageUp || Key == EKeys::PageDown)
		{
			if (const UMazeGridAsset* Asset = GetTargetAsset())
			{
				const int32 Delta = (Key == EKeys::PageUp) ? 1 : -1;
				Settings->ActiveDepthSlice = FMath::Clamp(
					Settings->ActiveDepthSlice + Delta, 0, FMath::Max(0, Asset->Grid.DepthCells() - 1));
				return true;
			}
		}

		if (Key == EKeys::LeftBracket || Key == EKeys::RightBracket)
		{
			const int32 Delta = (Key == EKeys::RightBracket) ? 1 : -1;
			Settings->BrushSize = FMath::Clamp(Settings->BrushSize + Delta, 1, 16);
			return true;
		}
	}

	// Peek: hold to suspend the isolation, release to go back to painting.
	//
	// Deliberately without the modifier checks, so the key keeps working mid-stroke and while
	// Shift or Ctrl are held. IE_Repeat is not accepted: a held key repeats, and the state is
	// already whatever the repeat would set it to.
	//
	// Not touched at all while isolation is off — there would be nothing to suspend, and the key
	// is better left to whatever the editor normally does with it.
	if (Settings->bIsolateActiveLayer && Settings->PeekKey.IsValid() && Key == Settings->PeekKey
		&& (Event == IE_Pressed || Event == IE_Released))
	{
		const bool bWanted = (Event == IE_Pressed);
		if (bWanted != bPeeking)
		{
			bPeeking = bWanted;
			RebuildPreview();

			if (ViewportClient)
			{
				ViewportClient->Invalidate(false, false);
			}
		}

		return true;
	}

	// Alt + LMB is the camera orbit. We do not intercept it.
	if (Key == EKeys::LeftMouseButton && !bAlt && GetTargetAsset())
	{
		// The object brush is a click, not a stroke, so it never enters the paint machinery:
		// no transaction spanning a drag, no box fill, nothing to leave open.
		if (Settings->Tool == EMazeEditTool::Objects)
		{
			if (Event == IE_Pressed)
			{
				bHasHover = ComputeCellUnderCursor(ViewportClient, HoveredCell);
				if (bHasHover && ApplyObjectClick(bShift) && ViewportClient)
				{
					ViewportClient->Invalidate(false, false);
				}
			}

			return true;
		}

		if (Event == IE_Pressed)
		{
			bHasHover = ComputeCellUnderCursor(ViewportClient, HoveredCell);
			if (bHasHover)
			{
				BeginStroke(bShift, bCtrl);
				return true;
			}
		}
		else if (Event == IE_Released && bPainting)
		{
			EndStroke();
			return true;
		}
	}

	return false;
}

bool UMazeEdMode::StartTracking(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	return bPainting;
}

bool UMazeEdMode::EndTracking(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	if (bPainting)
	{
		EndStroke();
		return true;
	}
	return false;
}

bool UMazeEdMode::DisallowMouseDeltaTracking() const
{
	// While a stroke is in progress the viewport must not read mouse motion as moving objects.
	return bPainting;
}

// ---------------------------------------------------------------- grid editing

void UMazeEdMode::GetDepthRange(const FMazeGrid& Grid, int32& OutMinY, int32& OutMaxY) const
{
	const int32 Total = Grid.DepthCells();

	// The back wall is a layer of its own and ignores Depth Apply entirely. It always goes into
	// its own slice at the far edge — otherwise a stroke in Fill Bands mode would smear the wall
	// across the whole depth and it would show up in front of the maze instead of behind it.
	if (Settings->PaintType == EMazeCellType::BackWall)
	{
		OutMinY = Grid.Depth.BackWallPlaneCellY();
		OutMaxY = OutMinY + 1;
		return;
	}

	switch (Settings->DepthApply)
	{
	case EMazeDepthApply::ActiveSliceOnly:
		OutMinY = Settings->ActiveDepthSlice;
		OutMaxY = Settings->ActiveDepthSlice + 1;
		break;

	case EMazeDepthApply::AllDepth:
		OutMinY = 0;
		OutMaxY = Total;
		break;

	case EMazeDepthApply::PlayBandOnly:
		OutMinY = Grid.Depth.PlayStartCell();
		OutMaxY = Grid.Depth.PlayEndCell();
		break;

	case EMazeDepthApply::FillBands:
	default:
		// The range comes from the bFill* flags; unsuitable bands are filtered out on write.
		OutMinY = 0;
		OutMaxY = Total;
		break;
	}

	OutMinY = FMath::Clamp(OutMinY, 0, Total);
	OutMaxY = FMath::Clamp(OutMaxY, 0, Total);
}

int32 UMazeEdMode::ApplyBrushAt(const FIntVector& Centre, bool bErase)
{
	const int32 Half = FMath::Max(0, Settings->BrushSize - 1) / 2;
	return ApplyBox(
		FIntVector(Centre.X - Half, 0, Centre.Z - Half),
		FIntVector(Centre.X + Half, 0, Centre.Z + Half),
		bErase);
}

int32 UMazeEdMode::ApplyBox(const FIntVector& A, const FIntVector& B, bool bErase)
{
	UMazeGridAsset* Asset = GetTargetAsset();
	if (!Asset || !Settings)
	{
		return 0;
	}

	FMazeGrid& Grid = Asset->Grid;

	int32 MinY = 0;
	int32 MaxY = 0;
	GetDepthRange(Grid, MinY, MaxY);

	const int32 MinX = FMath::Clamp(FMath::Min(A.X, B.X), 0, Grid.SizeXZ.X - 1);
	const int32 MaxX = FMath::Clamp(FMath::Max(A.X, B.X), 0, Grid.SizeXZ.X - 1);
	const int32 MinZ = FMath::Clamp(FMath::Min(A.Z, B.Z), 0, Grid.SizeXZ.Y - 1);
	const int32 MaxZ = FMath::Clamp(FMath::Max(A.Z, B.Z), 0, Grid.SizeXZ.Y - 1);

	// Fill flags describe the depth bands of the maze mass and say nothing about the back wall,
	// which lives in a single slice of its own.
	const bool bBackWall = (Settings->PaintType == EMazeCellType::BackWall);
	const bool bRespectFillFlags =
		!bBackWall && (Settings->DepthApply == EMazeDepthApply::FillBands);

	const FMazeCell NewCell(Settings->PaintType,
		static_cast<uint8>(FMath::Clamp(Settings->PaintVariant, 0, 255)));

	int32 Changed = 0;
	for (int32 X = MinX; X <= MaxX; ++X)
	{
		for (int32 Z = MinZ; Z <= MaxZ; ++Z)
		{
			for (int32 Y = MinY; Y < MaxY; ++Y)
			{
				if (bRespectFillFlags && !bErase && !Grid.Depth.IsFillBand(Grid.Depth.GetBand(Y)))
				{
					continue;
				}

				const FIntVector Cell(X, Y, Z);

				// The eraser stays on the layer being painted. Without this, rubbing out maze mass in
				// Fill Bands mode would also wipe the back wall behind it: that mode spans the whole
				// depth, and the fill flags only filter writes, not erases.
				if (bErase && !bBackWall && Grid.IsBackWall(Cell))
				{
					continue;
				}

				Changed += (bErase ? Grid.Clear(Cell) : Grid.Set(Cell, NewCell)) ? 1 : 0;
			}
		}
	}

	return Changed;
}

FString UMazeEdMode::ObjectHoverStatus(const UMazeGridAsset& Asset) const
{
	const UMazeObjectLibrary* Library = Settings ? Settings->GetObjectLibrary() : nullptr;
	const FMazeObjectType* Type = Library ? Library->FindType(Settings->PaintObjectType) : nullptr;

	if (!Type)
	{
		return TEXT("  |  NO OBJECT TYPE — pick one in the Objects palette");
	}

	const FMazeGrid& Grid = Asset.Grid;
	const int32 SliceY = MazePlacement::BandSliceY(Grid, Settings->PaintBand);
	const FIntPoint CellXZ(HoveredCell.X, HoveredCell.Z);

	const FString Reason = MazePlacement::DescribeMisfit(Grid, *Type, CellXZ, SliceY);

	// The rectangle under the cursor is FootprintCells, and FootprintCells is the clear space the
	// type asks for — not the size of the mesh, which nothing here has measured. They are the same
	// number often enough that the difference goes unnoticed until a 1x1 chandelier is drawn as one
	// tidy cell and spawns across half the room. Saying which of the two is on screen costs one
	// line and saves that afternoon.
	const FString Footprint = FString::Printf(TEXT("%dx%d cells needed"),
		FMath::Max(1, Type->FootprintCells.X), FMath::Max(1, Type->FootprintCells.Y));

	if (!Reason.IsEmpty())
	{
		return FString::Printf(TEXT("  |  %s WON'T FIT (%s): %s"),
			*Type->TypeId.ToString(), *Footprint, *Reason);
	}

	// The geometry has said yes; the other half of the question is whether the space is taken.
	//
	// Asked about the whole footprint and not about the cell under the cursor. The cursor sits
	// on one corner of the rectangle, so a wardrobe aimed just past a crate has a free corner
	// and lands on the crate anyway — and the cell-sized question answered "fits here", which
	// was worse than saying nothing, because it was an answer.
	//
	// Said, not enforced. The brush is allowed to be wrong on purpose: a crate half behind a
	// barrel is sometimes what the scene wants, and a brush that argues is a brush that gets
	// fought. What it may not do is call the space empty while something is standing in it.
	const UMazeSpawnAsset* Spawns = Settings ? Settings->GetSpawnAsset() : nullptr;
	const int32 Taken = Spawns
		? Spawns->FindOverlapping(Library, CellXZ, Type->FootprintCells)
		: INDEX_NONE;

	if (Spawns && Spawns->Placements.IsValidIndex(Taken))
	{
		return FString::Printf(TEXT("  |  %s OVERLAPS %s here (%s)"),
			*Type->TypeId.ToString(), *Spawns->Placements[Taken].TypeId.ToString(), *Footprint);
	}

	return FString::Printf(TEXT("  |  %s fits here (%s, mesh may be bigger)"),
		*Type->TypeId.ToString(), *Footprint);
}

bool UMazeEdMode::ApplyObjectClick(bool bErase)
{
	UMazeGridAsset* Asset = GetTargetAsset();
	UMazeSpawnAsset* Spawns = Settings ? Settings->GetSpawnAsset() : nullptr;
	UMazeObjectLibrary* Library = Settings ? Settings->GetObjectLibrary() : nullptr;

	if (!Asset || !Spawns)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Objects: the target has no Spawns asset set — nothing to place into."));
		return false;
	}

	const FIntPoint CellXZ(HoveredCell.X, HoveredCell.Z);

	if (bErase)
	{
		const int32 Index = Spawns->FindAtCell(Library, CellXZ);
		if (Index == INDEX_NONE)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("MazeEraseObject", "MazeForge: Erase Object"));
		Spawns->RemoveAt(Index);
		return true;
	}

	const FMazeObjectType* Type = Library ? Library->FindType(Settings->PaintObjectType) : nullptr;
	if (!Type)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Objects: no object type selected, or '%s' is not in the library."),
			*Settings->PaintObjectType.ToString());
		return false;
	}

	const FMazeGrid& Grid = Asset->Grid;
	const int32 SliceY = MazePlacement::BandSliceY(Grid, Settings->PaintBand);

	FMazePlacement Placement;
	Placement.TypeId = Type->TypeId;
	Placement.CellXZ = CellXZ;
	Placement.Band = Settings->PaintBand;

	// The hand is allowed to be wrong. A crate half sunk into a wall is sometimes exactly what
	// the scene wants, and a brush that refuses is a brush that argues. The generator is the one
	// that has to obey the rule, because it places blind and a hundred "never mind"s become a
	// hundred objects inside the mass.
	if (!MazePlacement::FindAnchor(Grid, *Type, CellXZ, SliceY, Placement.Anchor))
	{
		Placement.Anchor = MazePlacement::PreferredAnchor(*Type);

		UE_LOG(LogMazeForge, Warning,
			TEXT("Objects: '%s' does not fit at X %d Z %d (%s) — placed anyway, anchored as "
			     "declared. The export will NOT spawn it."),
			*Type->TypeId.ToString(), CellXZ.X, CellXZ.Y,
			*MazePlacement::DescribeMisfit(Grid, *Type, CellXZ, SliceY));
	}

	Placement.Rotation = MazePlacement::ResolveRotation(
		Grid, *Type, CellXZ, SliceY, Placement.Anchor, Asset->Grid.NumCells());

	const FScopedTransaction Transaction(LOCTEXT("MazePlaceObject", "MazeForge: Place Object"));
	Spawns->Add(Placement);
	return true;
}

void UMazeEdMode::AbortStroke()
{
	if (!bPainting)
	{
		return;
	}

	// This is the repair for a bug that made the whole mode look dead, and the mechanism is worth
	// spelling out. bPainting is not just bookkeeping: StartTracking returns it, so while it is
	// set the viewport believes this mode is dragging — it captures the mouse and hides the
	// cursor — and DisallowMouseDeltaTracking returns it too. Nothing cleared it except a mouse
	// release, so one swallowed release, or a panel button pressed mid-drag, left the mode stuck
	// with no cursor, no painting and a transaction open for the rest of the editor session.
	// Restarting the editor was the only cure, which is exactly what was observed.
	bPainting = false;
	bBoxDrag = false;
	bErasing = false;
	StrokeChangedCells = 0;

	if (GEditor)
	{
		GEditor->EndTransaction();
	}

	UE_LOG(LogMazeForge, Warning,
		TEXT("A stroke was left open and has been closed. If this repeats, say when — "
		     "it means a mouse release is going missing."));
}

void UMazeEdMode::BeginStroke(bool bInErase, bool bInBox)
{
	UMazeGridAsset* Asset = GetTargetAsset();
	if (!Asset)
	{
		return;
	}

	// Never nest one transaction inside another: a stroke that somehow survived its release is
	// closed here rather than being layered over.
	AbortStroke();

	bPainting = true;
	bErasing = bInErase;
	bBoxDrag = bInBox;
	DragStartCell = HoveredCell;
	StrokeChangedCells = 0;

	// One transaction per whole stroke: Ctrl+Z undoes the entire stroke, not a single cell.
	GEditor->BeginTransaction(bInErase
		? LOCTEXT("MazeErase", "MazeForge: Erase")
		: LOCTEXT("MazePaint", "MazeForge: Paint"));
	Asset->Modify();

	if (!bBoxDrag)
	{
		StrokeChangedCells += ApplyBrushAt(HoveredCell, bErasing);
	}
}

void UMazeEdMode::ContinueStroke()
{
	// The box fill is applied once, on button release.
	if (!bHasHover || bBoxDrag)
	{
		return;
	}

	const int32 Changed = ApplyBrushAt(HoveredCell, bErasing);
	StrokeChangedCells += Changed;

	if (Changed == 0)
	{
		return;
	}

	// During a stroke the preview is rebuilt no more often than once every 100 ms.
	//
	// Rebuild wipes every instance and walks the whole grid, while up to a hundred mouse moves
	// arrive per second. On a 707x376 map that meant a full walk over tens of thousands of cells
	// for every cursor pixel — drawing was impossible. The final state is rebuilt in EndStroke
	// anyway.
	constexpr double PreviewInterval = 0.1;
	const double Now = FPlatformTime::Seconds();

	if (Now - LastPreviewRebuildTime >= PreviewInterval)
	{
		LastPreviewRebuildTime = Now;
		RebuildPreview();
	}
}

void UMazeEdMode::EndStroke()
{
	UMazeGridAsset* Asset = GetTargetAsset();

	if (Asset && bBoxDrag && bHasHover)
	{
		StrokeChangedCells += ApplyBox(DragStartCell, HoveredCell, bErasing);
	}

	bPainting = false;
	bBoxDrag = false;

	GEditor->EndTransaction();

	if (Asset && StrokeChangedCells > 0)
	{
		Asset->NotifyGridChanged();
		UE_LOG(LogMazeForge, Verbose, TEXT("Stroke: %d cells changed, %d total."),
			StrokeChangedCells, Asset->Grid.NumCells());
	}

	StrokeChangedCells = 0;
	bErasing = false;
}

#undef LOCTEXT_NAMESPACE
