#include "Assets/MazeGridAssetFactory.h"

#include "Assets/MazeGridAsset.h"

UMazeGridAssetFactory::UMazeGridAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UMazeGridAsset::StaticClass();
}

UObject* UMazeGridAssetFactory::FactoryCreateNew(UClass* InClass,
                                                 UObject* InParent,
                                                 FName InName,
                                                 EObjectFlags Flags,
                                                 UObject* Context,
                                                 FFeedbackContext* Warn)
{
	UMazeGridAsset* Asset = NewObject<UMazeGridAsset>(InParent, InClass, InName, Flags);
	if (Asset)
	{
		// A new asset opens ready for hand-drawing right away.
		Asset->EnsureDefaults();
	}
	return Asset;
}
