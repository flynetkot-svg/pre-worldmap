#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "MazeGridAssetFactory.generated.h"

/** Creates a maze asset from the Content Browser. */
UCLASS()
class MAZEFORGEEDITOR_API UMazeGridAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UMazeGridAssetFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass,
	                                  UObject* InParent,
	                                  FName InName,
	                                  EObjectFlags Flags,
	                                  UObject* Context,
	                                  FFeedbackContext* Warn) override;

	virtual bool ShouldShowInNewMenu() const override { return true; }
};
