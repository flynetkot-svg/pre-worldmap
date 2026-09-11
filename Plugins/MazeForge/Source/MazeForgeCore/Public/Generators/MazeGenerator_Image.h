#pragma once

#include "CoreMinimal.h"
#include "Generators/MazeGeneratorBase.h"
#include "Data/MazeTypes.h"
#include "MazeGenerator_Image.generated.h"

class UTexture2D;

/**
 *  A "pixel colour -> cell type" rule.
 *
 *  The rules are data-driven and live in an array: to add, say, red as a spawn point the
 *  designer does not need a programmer. The first matching rule wins, so the order in
 *  the array matters.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeColorRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Rule")
	FLinearColor Color = FLinearColor::Black;

	/**
	 *  Match radius, 0..1. The comparison happens in display (sRGB) space, that is,
	 *  "this far away in the palette", not in linear space — otherwise one and the same
	 *  tolerance would behave differently on dark and on light colours.
	 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float Tolerance = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Rule")
	EMazeCellType Type = EMazeCellType::Solid;

	UPROPERTY(EditAnywhere, Category = "Rule")
	uint8 PaletteIndex = 0;
};

/**
 *  A maze from an image: a side view, black is walls, white is passages.
 *
 *  The X axis of the image maps onto world X, the Y axis of the image onto world Z (the
 *  height), because the image is a side view in the first place. The image origin is at
 *  the top and the world origin at the bottom, so by default the rows are flipped —
 *  otherwise the maze would stand upside down.
 */
UCLASS(DisplayName = "Image (side view bitmap)")
class MAZEFORGECORE_API UMazeGenerator_Image : public UMazeGeneratorBase
{
	GENERATED_BODY()

public:
	UMazeGenerator_Image();

	// ------------------------------------------------------------------- source

	/** The imported texture. Read from the asset's source data, not from a compressed mip. */
	UPROPERTY(EditAnywhere, Category = "Source")
	TSoftObjectPtr<UTexture2D> Texture;

	/**
	 *  A file on disk. Used when the texture is not set.
	 *
	 *  This way the maze can be drawn in any editor and Run Generator pressed without
	 *  importing the picture into the project on every iteration.
	 */
	UPROPERTY(EditAnywhere, Category = "Source",
		meta = (FilePathFilter = "Image (*.png;*.jpg;*.jpeg;*.bmp;*.tga)|*.png;*.jpg;*.jpeg;*.bmp;*.tga"))
	FFilePath SourceFile;

	// ------------------------------------------------------------------- layout

	/**
	 *  Target size of the maze in cells.
	 *
	 *  Set — the image is resampled to it, and the grid size is set from it as well.
	 *  Zero — the image is stretched over the whole current area of the grid. In the
	 *  second case the proportions of the image may be distorted, and that is a deliberate
	 *  trade-off: filling the given world completely matters more than keeping the aspect
	 *  ratio of the source.
	 */
	UPROPERTY(EditAnywhere, Category = "Layout", meta = (ClampMin = "0"))
	FIntPoint TargetSizeXZ = FIntPoint::ZeroValue;

	/** Flip vertically. On: the image has zero at the top, the world at the bottom. */
	UPROPERTY(EditAnywhere, Category = "Layout")
	bool bFlipVertical = true;

	// -------------------------------------------------------------------- rules

	/** By default: black -> Solid, white -> Empty. The designer adds to it. */
	UPROPERTY(EditAnywhere, Category = "Rules")
	TArray<FMazeColorRule> ColorRules;

	/** What a pixel that matched no rule is treated as. */
	UPROPERTY(EditAnywhere, Category = "Rules")
	EMazeCellType FallbackType = EMazeCellType::Empty;

	// --------------------------------------------------------------- validation

	/**
	 *  Report unreachable cavities in the log.
	 *
	 *  The image is hand-authored, so we do not touch the geometry: silently carving
	 *  passages into somebody else's drawing would be presumptuous. But we are obliged
	 *  to say there is a problem.
	 */
	UPROPERTY(EditAnywhere, Category = "Validation")
	bool bReportUnreachable = true;

	/** The player's height in cells. Only for the reachability check, changes no geometry. */
	UPROPERTY(EditAnywhere, Category = "Validation", meta = (ClampMin = "1", UIMax = "6"))
	int32 AgentHeightCells = 2;

	virtual FText GetDisplayName() const override;

protected:
	virtual void Generate(FMazeGrid& InOutGrid, FRandomStream& Rng) override;
};
