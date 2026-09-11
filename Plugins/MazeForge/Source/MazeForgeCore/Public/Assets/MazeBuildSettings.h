#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/MazeTypes.h"
#include "MazeBuildSettings.generated.h"

class UStaticMesh;
class UMaterialInterface;

/**
 *  One surface variant of a cell type, picked by FMazeCell::PaletteIndex.
 *
 *  Brick, rock, metal — the same kind of cell painted with different materials. The variant
 *  belongs to the data, not to the type: a second enum value per material would grow the paint
 *  type dropdown without end and would say nothing new about the geometry.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeSurfaceVariant
{
	GENERATED_BODY()

	/** Shown in the variant picker in the mode panel. Free-form. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variant")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variant")
	TSoftObjectPtr<UMaterialInterface> Material;

	/** Colour in the editor preview. Variants must differ, or the layers merge visually. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variant")
	FLinearColor EditorColor = FLinearColor::Gray;
};

/** What a single cell type is drawn with. */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeCellVisual
{
	GENERATED_BODY()

	/** Empty — the engine's stock cube is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TSoftObjectPtr<UMaterialInterface> MaterialOverride;

	/** Colour in the editor: preview, room bounds, brush highlight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor EditorColor = FLinearColor::Gray;

	/**
	 *  Surface variants of this type, addressed by FMazeCell::PaletteIndex.
	 *
	 *  Leave it empty and the type has exactly one surface — MaterialOverride above. Fill it and
	 *  index N picks Variants[N]; an index past the end falls back to the base surface, so
	 *  deleting a variant never breaks an already painted maze, it only makes it plainer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TArray<FMazeSurfaceVariant> Variants;
};

/**
 *  Palette and bake rules. A separate asset so that one set of settings can be
 *  reused by several mazes.
 */
UCLASS(BlueprintType)
class MAZEFORGECORE_API UMazeBuildSettings : public UDataAsset
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/**
	 *  Bumped whenever any build settings asset is edited.
	 *
	 *  Materials and bake flags live here, not in the grid, so editing them changes what every
	 *  room's mesh should look like without touching the grid's own revision. Anything that
	 *  caches a per-room decision has to notice that, and there is no cheap way for a grid asset
	 *  to watch a data asset it only holds a soft pointer to.
	 */
	static int32 GetEditRevision() { return EditRevision; }

private:
	static int32 EditRevision;

public:
#endif

public:
	UMazeBuildSettings();

	/** Cell type -> what to build it with. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	TMap<EMazeCellType, FMazeCellVisual> Palette;

	// ------------------------------------------------------- where assets are written
	//
	//  Paths from the content root, that is, they start with /Game. Everything the
	//  plugin generates goes only here: in a project with other people's assets the
	//  maze must tuck itself into its own corner and not mix with hand-made work.

	/** Where to put the room levels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output|Packages")
	FString LevelPackageRoot = TEXT("/Game/MazeForge/Maps");

	/** Where to put the baked room meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output|Packages")
	FString MeshPackageRoot = TEXT("/Game/MazeForge/Meshes");

	/**
	 *  Where to put the world manifest. Empty — next to the levels.
	 *
	 *  The manifest is the only thing the runtime reads, and keeping it away from two
	 *  hundred maps is more convenient: there is only one of it, but it has to be
	 *  looked up often.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output|Packages")
	FString ManifestPackageRoot;

	/** Name of the manifest asset. Change it if the project has several mazes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output|Packages")
	FString ManifestAssetName = TEXT("DA_MazeWorldManifest");

	// ---------------------------------------------------------- Outliner folders

	/**
	 *  Root Outliner folder for the generated actors.
	 *
	 *  Inside it the export creates one folder per room and puts the geometry into a
	 *  sub-folder. The designer places decor alongside, outside those folders, and can
	 *  then hide all the generated geometry of a room with the eye icon without
	 *  switching off the level itself.
	 *
	 *  Empty — do not arrange anything into folders at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output|Outliner")
	FString OutlinerRootFolder = TEXT("Levels");

	/**
	 *  Sub-folder for the meshes inside the room folder.
	 *
	 *  It is the reason the whole arrangement exists: one eye icon hides all the
	 *  generated geometry of a room, while the anchor and the decor stay visible.
	 *
	 *  Empty — the meshes land straight in the room folder.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output|Outliner")
	FString OutlinerMeshSubFolder = TEXT("MazeMeshes");

	/**
	 *  Sub-folder for the spawned objects inside the room folder.
	 *
	 *  Separate from the meshes for the same reason the meshes are separate from the decor:
	 *  hiding the walls to look at where the crates ended up is a thing you do constantly, and
	 *  it should be one eye icon and not a rubber-band selection.
	 *
	 *  Empty — the objects land straight in the room folder.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output|Outliner")
	FString OutlinerObjectSubFolder = TEXT("MazeObjects");

	/**
	 *  Create a separate folder for every room.
	 *
	 *  Turn it off for small mazes: two hundred folders help, a dozen only adds
	 *  clicks.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output|Outliner")
	bool bOutlinerFolderPerRoom = true;

	/**
	 *  Tag put on every generated actor.
	 *
	 *  A re-export deletes ONLY the tagged actors and does not touch the designer's
	 *  decor. Without this the very first edit of the maze would wipe out the team's work.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output")
	FName GeneratedTag = TEXT("MazeForge.Generated");

	/**
	 *  After how many rooms to unload the finished work from memory. Zero — never unload.
	 *
	 *  The export used to keep everything in memory at once, and at 208 rooms the editor
	 *  crashed AFTER a successful save: what ran out was not RAM but the RHI's reserved
	 *  address space for the resources of six hundred meshes. By the time of the unload
	 *  the rooms are already on disk and the manifest references them through soft
	 *  pointers — so unloading is safe, nothing is lost.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output", meta = (ClampMin = "0", UIMax = "64"))
	int32 RoomsPerFlush = 16;

	/** Split the depth bands into separate actors: collision only on Play, fade for Foreground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bake")
	bool bSplitByDepthBand = true;

	/** Merge materials into an atlas during the merge. More expensive in time, cheaper in draw calls. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bake")
	bool bMergeMaterials = false;

	/** Throw away cells fully surrounded by solid. The main geometry saving. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bake")
	bool bCullEnclosedCells = true;

	/**
	 *  Do not build the far world face along −Y.
	 *
	 *  The camera sits at +Y and looks inwards, so in the game this side is not visible,
	 *  and it saves a whole layer of quads per room. But the mesh then stays open, and a
	 *  free camera in debugging will see a hole. Off by default: correct geometry matters
	 *  more, turn it on deliberately for the sake of the budget.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bake")
	bool bCullFarBoundaryFaces = false;

	const FMazeCellVisual* FindVisual(EMazeCellType Type) const { return Palette.Find(Type); }

	/**
	 *  The material for a cell, variant taken into account. Null means the engine default.
	 *
	 *  Resolution lives here and nowhere else: the mesh bake and the editor preview have to agree
	 *  on which surface a cell belongs to, otherwise the preview lies about the result.
	 */
	TSoftObjectPtr<UMaterialInterface> ResolveMaterial(EMazeCellType Type, uint8 Variant) const;

	/** The editor colour for a cell, variant taken into account. */
	FLinearColor ResolveEditorColor(EMazeCellType Type, uint8 Variant) const;

	/** How many surface variants the type declares. One means no variants at all. */
	int32 NumVariants(EMazeCellType Type) const;
};
