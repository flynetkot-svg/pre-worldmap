#include "Assets/MazeBuildSettings.h"

#if WITH_EDITOR
int32 UMazeBuildSettings::EditRevision = 0;

void UMazeBuildSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	++EditRevision;
}
#endif

UMazeBuildSettings::UMazeBuildSettings()
{
	// The maze mass ships with variants of its own: one stretch of a level is brick, another is
	// rock, another is the wall of a building. Same geometry, different surface — exactly what the
	// variant index is for, and a reason not to invent a cell type per material.
	FMazeCellVisual SolidVisual;
	SolidVisual.EditorColor = FLinearColor(0.35f, 0.35f, 0.38f);

	FMazeSurfaceVariant SolidBrick;
	SolidBrick.Name = TEXT("Brick");
	SolidBrick.EditorColor = FLinearColor(0.52f, 0.26f, 0.18f);

	FMazeSurfaceVariant SolidStone;
	SolidStone.Name = TEXT("Stone");
	SolidStone.EditorColor = FLinearColor(0.38f, 0.38f, 0.36f);

	FMazeSurfaceVariant SolidBuilding;
	SolidBuilding.Name = TEXT("Building");
	SolidBuilding.EditorColor = FLinearColor(0.30f, 0.36f, 0.44f);

	SolidVisual.Variants = { SolidBrick, SolidStone, SolidBuilding };
	Palette.Add(EMazeCellType::Solid, SolidVisual);

	FMazeCellVisual FloorVisual;
	FloorVisual.EditorColor = FLinearColor(0.45f, 0.38f, 0.28f);
	Palette.Add(EMazeCellType::Floor, FloorVisual);

	// The back wall ships with three variants out of the box. Their materials are left empty —
	// the designer fills those in — but the editor colours are set and deliberately far apart
	// from the maze grey: the whole point of a separate brush is that the wall behind the maze
	// must not blend into the maze itself while you are drawing.
	FMazeCellVisual BackWallVisual;
	BackWallVisual.EditorColor = FLinearColor(0.18f, 0.20f, 0.30f);

	FMazeSurfaceVariant Brick;
	Brick.Name = TEXT("Brick");
	Brick.EditorColor = FLinearColor(0.40f, 0.18f, 0.14f);

	FMazeSurfaceVariant Rock;
	Rock.Name = TEXT("Rock");
	Rock.EditorColor = FLinearColor(0.22f, 0.26f, 0.20f);

	FMazeSurfaceVariant Metal;
	Metal.Name = TEXT("Metal");
	Metal.EditorColor = FLinearColor(0.20f, 0.30f, 0.40f);

	BackWallVisual.Variants = { Brick, Rock, Metal };
	Palette.Add(EMazeCellType::BackWall, BackWallVisual);
}

TSoftObjectPtr<UMaterialInterface> UMazeBuildSettings::ResolveMaterial(EMazeCellType Type,
                                                                      uint8 Variant) const
{
	const FMazeCellVisual* Visual = FindVisual(Type);
	if (!Visual)
	{
		return nullptr;
	}

	// An index past the end is not an error: variants can be deleted from the palette while a
	// maze painted with them already exists. Falling back to the base surface keeps that maze
	// buildable — it just comes out plainer than it was drawn.
	if (Visual->Variants.IsValidIndex(Variant))
	{
		return Visual->Variants[Variant].Material;
	}

	return Visual->MaterialOverride;
}

FLinearColor UMazeBuildSettings::ResolveEditorColor(EMazeCellType Type, uint8 Variant) const
{
	const FMazeCellVisual* Visual = FindVisual(Type);
	if (!Visual)
	{
		return FLinearColor::Gray;
	}

	if (Visual->Variants.IsValidIndex(Variant))
	{
		return Visual->Variants[Variant].EditorColor;
	}

	return Visual->EditorColor;
}

int32 UMazeBuildSettings::NumVariants(EMazeCellType Type) const
{
	const FMazeCellVisual* Visual = FindVisual(Type);
	return (Visual && Visual->Variants.Num() > 0) ? Visual->Variants.Num() : 1;
}
