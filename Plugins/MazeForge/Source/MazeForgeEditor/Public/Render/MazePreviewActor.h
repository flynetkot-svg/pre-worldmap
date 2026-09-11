#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/MazeTypes.h"
#include "MazePreviewActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMazeGridAsset;

/**
 *  Preview of the drawn maze in the editor.
 *
 *  One HISM component per cell type: rebuilding is instant and creates no assets.
 *  The final geometry is a merged Static Mesh produced at the export stage (P2);
 *  the preview only exists while the mode is open and is never saved into the level.
 */
UCLASS(NotPlaceable, Transient, NotBlueprintable)
class MAZEFORGEEDITOR_API AMazePreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AMazePreviewActor();

	/**
	 *  A full rebuild of the instances from the asset's contents.
	 *
	 *  SkipRoomIds lists the rooms whose levels are already loaded in the editor. The preview
	 *  does not duplicate their geometry: two copies of the same surfaces in one place fight
	 *  over depth and cause flickering.
	 */
	/**
	 *  @param bFinishTreesNow  build the instancing cluster trees synchronously before returning.
	 *
	 *  A hierarchical instanced component draws nothing until its cluster tree is built, and by
	 *  default that build is asynchronous. Mid-stroke that is exactly right — the mouse invalidates
	 *  the viewport on every move, so the next frame picks the tree up as soon as it is ready. Off
	 *  the mouse it is exactly wrong: the caller invalidates once, the tree is not ready yet, and
	 *  when it becomes ready there is nothing left to ask for another frame.
	 */
	void Rebuild(const UMazeGridAsset* Asset, const TSet<FName>& SkipRoomIds = TSet<FName>(),
	             EMazeCellType ActiveType = EMazeCellType::Empty, bool bIsolateActive = false,
	             bool bFinishTreesNow = false, float InactiveDim = 0.45f);

	void ClearInstances();

private:
	/**
	 *  One component per surface — cell type plus palette variant.
	 *
	 *  The key is packed into an int rather than kept as a struct: a TMap key needs GetTypeHash,
	 *  and a whole struct for two bytes of data would be more ceremony than the thing is worth.
	 */
	UPROPERTY()
	TMap<int32, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> CellComponents;

	/**
	 *  The component for one surface. The colour is applied on every rebuild, not only on creation:
	 *  a layer dims and brightens as the paint type changes, and the component is reused.
	 */
	UHierarchicalInstancedStaticMeshComponent* GetOrCreateLayer(EMazeCellType Type, uint8 Variant,
	                                                           const FLinearColor& Color);

	static void ApplyLayerColor(UHierarchicalInstancedStaticMeshComponent* Component,
	                            const FLinearColor& Color);

	static int32 MakeLayerKey(EMazeCellType Type, uint8 Variant)
	{
		return (static_cast<int32>(Type) << 8) | Variant;
	}
};
