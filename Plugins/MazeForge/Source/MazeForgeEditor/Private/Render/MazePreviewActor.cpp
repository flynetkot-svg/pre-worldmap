#include "Render/MazePreviewActor.h"

#include "Assets/MazeGridAsset.h"
#include "Data/MazeRoomDesc.h"
#include "Assets/MazeBuildSettings.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "MazeForgeCore.h"

namespace
{
	/** The engine's standard cube: 100x100x100, pivot at the centre. */
	const TCHAR* DefaultCubePath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/**
	 *  Engine content with a Color parameter — the cheapest way to tint the preview.
	 *
	 *  The plugin ships no content of its own (CanContainContent is false in the .uplugin), so a
	 *  material of ours would have to live in the project and could go missing. This one is part
	 *  of the engine and is always there.
	 */
	const TCHAR* ColorMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	const FName ColorParameterName(TEXT("Color"));

	/**
	 *  What a dimmed layer is blended towards.
	 *
	 *  Not black, and that is the point. Scaling a colour towards black keeps bright surfaces
	 *  readable and erases dark ones — the back wall ships at around 0.2 per channel, and a
	 *  third of that is the viewport background. Blending towards a dark grey subdues every
	 *  colour by the same visual amount, whatever it started at.
	 */
	const FLinearColor DimTarget(0.10f, 0.10f, 0.11f);
}

AMazePreviewActor::AMazePreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
#if WITH_EDITORONLY_DATA
	bIsEditorOnlyActor = true;
	// The preview has no business being in the Outliner: it only lives while the mode is open
	// and is not saved into the level. A row in the list would only get in the way of finding
	// the decoration.
	bListedInSceneOutliner = false;
#endif
}

UHierarchicalInstancedStaticMeshComponent* AMazePreviewActor::GetOrCreateLayer(
	EMazeCellType Type, uint8 Variant, const FLinearColor& Color)
{
	const int32 Key = MakeLayerKey(Type, Variant);

	if (TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* Existing = CellComponents.Find(Key))
	{
		return *Existing;
	}

	UHierarchicalInstancedStaticMeshComponent* Component =
		NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);

	Component->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, DefaultCubePath));
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetupAttachment(GetRootComponent());
	Component->RegisterComponent();

	// Tinting the preview is the whole reason the back wall got its own brush: on a plain grey
	// cube the wall behind the maze is indistinguishable from the maze itself.
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, ColorMaterialPath))
	{
		if (UMaterialInstanceDynamic* Tinted = UMaterialInstanceDynamic::Create(Base, Component))
		{
			Component->SetMaterial(0, Tinted);
		}
	}

	CellComponents.Add(Key, Component);
	ApplyLayerColor(Component, Color);
	return Component;
}

void AMazePreviewActor::ApplyLayerColor(UHierarchicalInstancedStaticMeshComponent* Component,
                                        const FLinearColor& Color)
{
	if (!Component)
	{
		return;
	}

	// Set on every rebuild rather than at creation: which layer is dimmed depends on the paint
	// type, the type changes constantly, and the components are reused across rebuilds.
	if (UMaterialInstanceDynamic* Tinted = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0)))
	{
		Tinted->SetVectorParameterValue(ColorParameterName, Color);
	}
}

void AMazePreviewActor::ClearInstances()
{
	for (TPair<int32, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& Pair : CellComponents)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearInstances();

			// Back to the origin before refilling. Instances are added in world space, and that
			// conversion goes through the component transform — leaving a stale isolation offset on
			// it would silently bake the shift into the instances themselves.
			Pair.Value->SetRelativeLocation(FVector::ZeroVector);
		}
	}
}

void AMazePreviewActor::Rebuild(const UMazeGridAsset* Asset, const TSet<FName>& SkipRoomIds,
                                EMazeCellType ActiveType, bool bIsolateActive, bool bFinishTreesNow,
                                float InactiveDim)
{
	ClearInstances();

	if (!Asset)
	{
		return;
	}

	const FMazeGrid& Grid = Asset->Grid;

	// The rectangles of the rooms that are already represented by real geometry in the levels.
	TArray<FMazeRoomDesc> SkipRects;
	if (SkipRoomIds.Num() > 0)
	{
		for (const FMazeRoomDesc& Room : Asset->Rooms)
		{
			if (SkipRoomIds.Contains(Room.RoomId))
			{
				SkipRects.Add(Room);
			}
		}
	}

	auto IsInsideExportedRoom = [&SkipRects](const FIntVector& Cell)
	{
		for (const FMazeRoomDesc& Room : SkipRects)
		{
			if (Room.ContainsXZ(Cell.X, Cell.Z))
			{
				return true;
			}
		}
		return false;
	};

	// The engine's cube is exactly 100 cm, so the scale is the cell size divided by 100.
	const FVector Scale = Grid.CellSize / 100.0;

	TMap<int32, TArray<FTransform>> Batches;
	int32 Culled = 0;

	// Isolation moves the layers that are NOT being painted away from the camera, and leaves the
	// layer being drawn exactly where it belongs.
	//
	// It is done by moving the layer's COMPONENT, not the instances inside it, and that detail is
	// the whole point. Two earlier attempts shifted the instance transforms themselves, and both
	// times a layer went missing until the viewport was switched to perspective and back: a
	// hierarchical instanced component culls against a cluster tree built from the instance data,
	// and instances that jump a long way leave that tree describing where they used to be. Moving
	// the component instead is an ordinary transform change — bounds and culling follow it the way
	// they follow any other moved component, and the instance data never changes at all.
	const double IsolateOffsetY = Grid.DepthCells() * Grid.CellSize.Y;

	const bool bIsolate = bIsolateActive && ActiveType != EMazeCellType::Empty;

	// The palette decides the colours. Without it the preview still draws, just in one grey —
	// that is the case of a maze asset with no Build Settings assigned yet.
	const UMazeBuildSettings* Settings = Asset->BuildSettings.LoadSynchronous();

	// In a flat grid there are no enclosed cells by construction: they simply have no
	// neighbours along the depth axis, and the check always returns false. Yet it costs six
	// TMap lookups per cell — and while rebuilding during a stroke that is exactly the
	// difference between "it draws" and "it does not".
	const bool bCanBeEnclosed = Grid.DepthCells() > 1;

	for (const TPair<FIntVector, FMazeCell>& Pair : Grid.Cells)
	{
		// A cell closed off by solids on every side is not visible — we do not build it in the
		// preview either. The same culling later runs in the export and removes the bulk of
		// the geometry.
		if (bCanBeEnclosed && Pair.Value.IsSolid() && Grid.IsFullyEnclosed(Pair.Key))
		{
			++Culled;
			continue;
		}

		if (IsInsideExportedRoom(Pair.Key))
		{
			continue;
		}

		// Instances always carry their true position. Isolation moves the whole layer component
		// afterwards, not the cells inside it.
		Batches.FindOrAdd(MakeLayerKey(Pair.Value.Type, Pair.Value.PaletteIndex)).Add(
			FTransform(FRotator::ZeroRotator, Grid.CellToWorld(Pair.Key), Scale));
	}

	// The backdrop is derived geometry, it is not among the drawn cells. We show it as a single
	// stretched cube rather than cell by cell: on a 256x128 map that would be another ten
	// thousand instances for what is one solid slab.
	const bool bAnythingExported = SkipRects.Num() > 0;

	const int32 BackdropCells = FMath::Min(Grid.Depth.BackdropCells, Grid.Depth.BackgroundCells);
	if (!bAnythingExported && BackdropCells > 0 && Grid.SizeXZ.X > 0 && Grid.SizeXZ.Y > 0)
	{
		const FBox Backdrop =
			Grid.GetCellBounds(FIntVector(0, 0, 0))
			+ Grid.GetCellBounds(FIntVector(Grid.SizeXZ.X - 1, BackdropCells - 1, Grid.SizeXZ.Y - 1));

		Batches.FindOrAdd(MakeLayerKey(EMazeCellType::Solid, 0)).Add(
			FTransform(FRotator::ZeroRotator, Backdrop.GetCenter(), Backdrop.GetSize() / 100.0));
	}

	// The world borders are derived geometry too. We draw them as four slabs so that the
	// designer sees in the editor exactly what will go into the export.
	if (!bAnythingExported && Grid.SizeXZ.X > 0 && Grid.SizeXZ.Y > 0)
	{
		const int32 T = FMath::Max(1, Grid.Borders.ThicknessCells);
		const int32 MaxX = Grid.SizeXZ.X - 1;
		const int32 MaxZ = Grid.SizeXZ.Y - 1;
		const int32 LastY = Grid.DepthCells() - 1;

		auto AddSlab = [&Batches, &Grid](const FIntVector& From, const FIntVector& To)
		{
			const FBox Slab = Grid.GetCellBounds(From) + Grid.GetCellBounds(To);
			Batches.FindOrAdd(MakeLayerKey(EMazeCellType::Solid, 0)).Add(
				FTransform(FRotator::ZeroRotator, Slab.GetCenter(), Slab.GetSize() / 100.0));
		};

		if (Grid.Borders.bCloseLeft)
		{
			AddSlab(FIntVector(0, 0, 0), FIntVector(T - 1, LastY, MaxZ));
		}
		if (Grid.Borders.bCloseRight)
		{
			AddSlab(FIntVector(MaxX - T + 1, 0, 0), FIntVector(MaxX, LastY, MaxZ));
		}
		if (Grid.Borders.bCloseBottom)
		{
			AddSlab(FIntVector(0, 0, 0), FIntVector(MaxX, LastY, T - 1));
		}
		if (Grid.Borders.bCloseTop)
		{
			AddSlab(FIntVector(0, 0, MaxZ - T + 1), FIntVector(MaxX, LastY, MaxZ));
		}
	}

	for (TPair<int32, TArray<FTransform>>& Batch : Batches)
	{
		const EMazeCellType Type = static_cast<EMazeCellType>(Batch.Key >> 8);
		const uint8 Variant = static_cast<uint8>(Batch.Key & 0xFF);

		FLinearColor Color = Settings
			? Settings->ResolveEditorColor(Type, Variant)
			: FLinearColor::Gray;

		// Bringing the active layer forward is only half the job: on top of a bright maze the wall
		// behind it still reads as noise. Holding the rest back is what makes it readable.
		if (bIsolate && Type != ActiveType)
		{
			Color = FMath::Lerp(DimTarget, Color, FMath::Clamp(InactiveDim, 0.0f, 1.0f));
		}

		UHierarchicalInstancedStaticMeshComponent* Layer = GetOrCreateLayer(Type, Variant, Color);
		ApplyLayerColor(Layer, Color);
		Layer->AddInstances(Batch.Value, false, true);

		// After filling, never before: the instances above are given in world space, and that
		// conversion reads the component transform as it is at that moment.
		if (bIsolate && Type != ActiveType)
		{
			Layer->SetRelativeLocation(FVector(0.0, -IsolateOffsetY, 0.0));
		}

		// Synchronously when asked. See the comment on the declaration: an asynchronous tree that
		// finishes after the last redraw leaves the layer invisible until something else asks for
		// a frame — which is why pressing a button in the panel used to blank the preview.
		if (bFinishTreesNow)
		{
			Layer->BuildTreeIfOutdated(/*Async*/ false, /*ForceUpdate*/ false);
		}
	}

	UE_LOG(LogMazeForge, Verbose, TEXT("Preview: %d instances, %d hidden ones culled."),
		Grid.Cells.Num() - Culled, Culled);
}
