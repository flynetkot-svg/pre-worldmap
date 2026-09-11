#include "Generators/MazeGenerator_Image.h"

#include "Data/MazeGrid.h"
#include "Engine/Texture2D.h"
#include "Generators/MazeLayout2D.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "MazeForgeCore.h"

#define LOCTEXT_NAMESPACE "MazeForge"

namespace
{
	/** A rule reduced to a display colour: the conversion from linear is done once. */
	struct FResolvedRule
	{
		FColor Color = FColor::Black;
		float ToleranceSq = 0.0f;
		EMazeCellType Type = EMazeCellType::Solid;
	};

	EMazeCellType ClassifyPixel(const FColor& Pixel, const TArray<FResolvedRule>& Rules,
	                            EMazeCellType Fallback)
	{
		for (const FResolvedRule& Rule : Rules)
		{
			const float DR = (static_cast<float>(Pixel.R) - Rule.Color.R) / 255.0f;
			const float DG = (static_cast<float>(Pixel.G) - Rule.Color.G) / 255.0f;
			const float DB = (static_cast<float>(Pixel.B) - Rule.Color.B) / 255.0f;

			if (DR * DR + DG * DG + DB * DB <= Rule.ToleranceSq)
			{
				return Rule.Type;
			}
		}

		return Fallback;
	}
}

UMazeGenerator_Image::UMazeGenerator_Image()
{
	// The starting set from the spec: black is walls, white is passages. Everything else
	// the designer adds themselves, without touching the code.
	FMazeColorRule Wall;
	Wall.Color = FLinearColor::Black;
	Wall.Tolerance = 0.25f;
	Wall.Type = EMazeCellType::Solid;

	FMazeColorRule Floor;
	Floor.Color = FLinearColor::White;
	Floor.Tolerance = 0.25f;
	Floor.Type = EMazeCellType::Empty;

	ColorRules.Add(Wall);
	ColorRules.Add(Floor);
}

FText UMazeGenerator_Image::GetDisplayName() const
{
	return LOCTEXT("GeneratorImage", "Image");
}

void UMazeGenerator_Image::Generate(FMazeGrid& InOutGrid, FRandomStream& Rng)
{
	// --- 1. Fetch the pixels. Both roads, the asset and the file, meet in an FImage.

	FImage Source;
	bool bLoaded = false;
	FString SourceName;

#if WITH_EDITOR
	if (UTexture2D* LoadedTexture = Texture.LoadSynchronous())
	{
		// Source specifically, not PlatformData: in the platform data the mip is compressed
		// to DXT, and the colours drift after decompression — the rules stop matching.
		bLoaded = LoadedTexture->Source.GetMipImage(Source, 0);
		SourceName = LoadedTexture->GetName();

		if (!bLoaded)
		{
			UE_LOG(LogMazeForge, Warning,
				TEXT("Image: failed to read the source data of texture %s."), *SourceName);
		}
	}
#endif

	if (!bLoaded && !SourceFile.FilePath.IsEmpty())
	{
		bLoaded = FImageUtils::LoadImage(*SourceFile.FilePath, Source);
		SourceName = FPaths::GetCleanFilename(SourceFile.FilePath);

		if (!bLoaded)
		{
			UE_LOG(LogMazeForge, Warning,
				TEXT("Image: failed to read the file %s."), *SourceFile.FilePath);
		}
	}

	if (!bLoaded)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Image: no source set. Specify a texture or a file on disk."));
		return;
	}

	// We bring it to a single format: from here on the code works with FColor and does not
	// think about the original colour depth, channels and gamma.
	FImage Rgba;
	Source.CopyTo(Rgba, ERawImageFormat::BGRA8, EGammaSpace::sRGB);

	const TArrayView64<const FColor> Pixels = Rgba.AsBGRA8();
	const int32 SourceW = static_cast<int32>(Rgba.GetWidth());
	const int32 SourceH = static_cast<int32>(Rgba.GetHeight());

	if (SourceW <= 0 || SourceH <= 0 || Pixels.Num() < static_cast<int64>(SourceW) * SourceH)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Image: the picture %s is empty."), *SourceName);
		return;
	}

	// --- 2. Target size.

	FIntPoint Target = TargetSizeXZ;
	if (Target.X > 0 && Target.Y > 0)
	{
		// An explicit size wins and adjusts the grid to itself.
		InOutGrid.SizeXZ = Target;
	}
	else
	{
		Target = InOutGrid.SizeXZ;
	}

	if (Target.X <= 0 || Target.Y <= 0)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Image: the target size %dx%d is invalid."),
			Target.X, Target.Y);
		return;
	}

	// --- 3. The rules reduced to display colours.

	TArray<FResolvedRule> Rules;
	Rules.Reserve(ColorRules.Num());

	for (const FMazeColorRule& Rule : ColorRules)
	{
		FResolvedRule Resolved;
		Resolved.Color = Rule.Color.ToFColor(true);
		Resolved.ToleranceSq = Rule.Tolerance * Rule.Tolerance;
		Resolved.Type = Rule.Type;
		Rules.Add(Resolved);
	}

	// --- 4. Resampling the image into a layout.

	FMazeLayout2D Layout(Target);

	// Counters per cell type: EMazeCellType currently has six values.
	constexpr int32 TypeCount = 6;

	for (int32 TZ = 0; TZ < Target.Y; ++TZ)
	{
		// The image has zero at the top and the world at the bottom — without the flip the
		// maze would stand upside down and the floor would end up being the ceiling.
		const int32 SampleRow = bFlipVertical ? (Target.Y - 1 - TZ) : TZ;

		const int32 SY0 = (SampleRow * SourceH) / Target.Y;
		const int32 SY1 = FMath::Max(SY0 + 1, ((SampleRow + 1) * SourceH) / Target.Y);

		for (int32 TX = 0; TX < Target.X; ++TX)
		{
			const int32 SX0 = (TX * SourceW) / Target.X;
			const int32 SX1 = FMath::Max(SX0 + 1, ((TX + 1) * SourceW) / Target.X);

			// A majority vote over the block of source pixels. When scaling up, the block
			// degenerates into a single pixel, and that is an honest nearest: interpolating
			// the colour here is not allowed — a mix of black and white is not a cell type.
			int32 Votes[TypeCount] = {};

			for (int32 SY = SY0; SY < SY1 && SY < SourceH; ++SY)
			{
				for (int32 SX = SX0; SX < SX1 && SX < SourceW; ++SX)
				{
					const EMazeCellType Type =
						ClassifyPixel(Pixels[static_cast<int64>(SY) * SourceW + SX], Rules, FallbackType);

					const int32 TypeIndex = static_cast<int32>(Type);
					if (TypeIndex >= 0 && TypeIndex < TypeCount)
					{
						++Votes[TypeIndex];
					}
				}
			}

			int32 BestIndex = static_cast<int32>(FallbackType);
			int32 BestVotes = 0;

			for (int32 Index = 0; Index < TypeCount; ++Index)
			{
				if (Votes[Index] > BestVotes)
				{
					BestVotes = Votes[Index];
					BestIndex = Index;
				}
			}

			Layout.Set(TX, TZ, static_cast<EMazeCellType>(BestIndex));
		}
	}

	// --- 5. Reachability check. Report only: the drawing is someone else's, do not edit it.

	int32 UnreachableIslands = 0;

	if (bReportUnreachable)
	{
		const int32 AgentHeight = FMath::Max(1, AgentHeightCells);

		// As the start we take the first cell from the top that the player fits into:
		// entering the maze from above is the same convention as in the random generator.
		FIntPoint Start(INDEX_NONE, INDEX_NONE);
		for (int32 Z = Target.Y - 1; Z >= 0 && Start.X == INDEX_NONE; --Z)
		{
			for (int32 X = 0; X < Target.X; ++X)
			{
				if (Layout.CanStand(X, Z, AgentHeight))
				{
					Start = FIntPoint(X, Z);
					break;
				}
			}
		}

		if (Start.X != INDEX_NONE)
		{
			TBitArray<> Standable;
			Layout.FloodFill(Start, AgentHeight, Standable);

			TArray<FIntPoint> Islands;
			TArray<int32> IslandSizes;
			Layout.FindIslands(Standable, AgentHeight, Islands, IslandSizes);

			UnreachableIslands = Islands.Num();

			if (UnreachableIslands > 0)
			{
				UE_LOG(LogMazeForge, Warning,
					TEXT("Image: %d unreachable cavities, the first one at X %d Z %d. ")
					TEXT("I am not touching the geometry — it is your drawing, add the passages with the brush."),
					UnreachableIslands, Islands[0].X, Islands[0].Y);
			}
		}
	}

	// --- 6. The flat solution is stretched along the depth.

	MazeWriteLayoutToGrid(Layout, InOutGrid);

	UE_LOG(LogMazeForge, Log,
		TEXT("Image: %s %dx%d -> grid %dx%d, %d rules, %d unreachable cavities."),
		*SourceName, SourceW, SourceH, Target.X, Target.Y, Rules.Num(), UnreachableIslands);
}

#undef LOCTEXT_NAMESPACE
