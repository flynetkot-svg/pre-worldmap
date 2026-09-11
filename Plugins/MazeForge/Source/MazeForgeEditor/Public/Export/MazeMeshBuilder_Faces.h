#pragma once

#include "CoreMinimal.h"
#include "Export/MazeMeshBuilder.h"

/**
 *  Bakes a room mesh from the visible faces only.
 *
 *  A cube gives 12 triangles, and not one of them is visible inside a solid.
 *  So we do not build cubes at all: for every solid cell we build only the faces that have
 *  empty space behind them. In densely built areas that removes 80-95% of the geometry —
 *  exactly the budget the testers will later be measuring frame time against.
 */
class FMazeMeshBuilder_Faces : public IMazeMeshBuilder
{
public:
	virtual bool Build(const FMazeBakeRequest& Request, FMazeBakeResult& OutResult) override;

	virtual FString GetDisplayName() const override { return TEXT("Face Culling"); }
};
