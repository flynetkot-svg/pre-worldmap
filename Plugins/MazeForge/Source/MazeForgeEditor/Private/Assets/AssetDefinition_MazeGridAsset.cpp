#include "AssetDefinition_MazeGridAsset.h"

#include "Assets/MazeGridAsset.h"

#define LOCTEXT_NAMESPACE "MazeForgeEditor"

FText UAssetDefinition_MazeGridAsset::GetAssetDisplayName() const
{
	return LOCTEXT("MazeGridAssetName", "Maze Grid");
}

FLinearColor UAssetDefinition_MazeGridAsset::GetAssetColor() const
{
	return FLinearColor(FColor(200, 120, 40));
}

TSoftClassPtr<UObject> UAssetDefinition_MazeGridAsset::GetAssetClass() const
{
	return UMazeGridAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_MazeGridAsset::GetAssetCategories() const
{
	static const auto Categories = { FAssetCategoryPath(LOCTEXT("MazeForgeCategory", "MazeForge")) };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
