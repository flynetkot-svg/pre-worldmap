#pragma once

#include "CoreMinimal.h"
#include "Generators/MazeGeneratorBase.h"
#include "Data/MazeTypes.h"
#include "MazeGenerator_Mesh.generated.h"

class UStaticMesh;

/**
 *  Rebuilding a maze from a finished flat mesh.
 *
 *  The input is a maze assembled from cubes and welded into a single mesh, with no depth
 *  and with no filled-in unreachable areas. The output is the same layout one to one, but
 *  with the cavities filled in and with a volume along Y stretched through the grid's
 *  depth profile.
 *
 *  The occupancy criterion is "the centre of the cell fell inside the projection of a
 *  triangle", and that is fundamental. The "the triangle touched the cell" test, on
 *  geometry aligned to the very same grid, marks both cells on either side of a face and
 *  doubles the wall thickness. A parity fill requires a closed volume, which a welded
 *  mesh is not. Both variants were tried on a real maze and gave the wrong result.
 */
UCLASS(DisplayName = "Static Mesh (rebuild flat maze)")
class MAZEFORGECORE_API UMazeGenerator_Mesh : public UMazeGeneratorBase
{
	GENERATED_BODY()

public:
	/** The finished maze as a single mesh. Taken in its own local space. */
	UPROPERTY(EditAnywhere, Category = "Rebuild")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/**
	 *  Size of the cube in centimetres — the same one the source maze was assembled from.
	 *
	 *  The grid size and its CellSize are derived from it and from the mesh bounds. The
	 *  depth is taken not from here but from the Background / Play / Foreground profile:
	 *  the volume along Y is grown, not copied from the flat source.
	 */
	UPROPERTY(EditAnywhere, Category = "Rebuild", meta = (ClampMin = "1", AllowPreserveRatio))
	FVector VoxelSize = FVector(50.0, 50.0, 50.0);

	/** The type the occupied cells are filled with. */
	UPROPERTY(EditAnywhere, Category = "Rebuild")
	EMazeCellType SolidType = EMazeCellType::Solid;

	// --------------------------------------------------------------------- fill

	/** Fill with solid everything the player cannot get into. */
	UPROPERTY(EditAnywhere, Category = "Fill")
	bool bFillUnreachable = true;

	/**
	 *  The player's height in cells. Defines what counts as reachable.
	 *
	 *  The stock UE capsule is 176 cm, that is two cells of 100 or four of 50.
	 *  A gap lower than this height is impassable, and the cavity behind it will be filled.
	 */
	UPROPERTY(EditAnywhere, Category = "Fill", meta = (ClampMin = "1", UIMax = "8"))
	int32 AgentHeightCells = 4;

	/** Which LOD we take apart. Zero — the source one. */
	UPROPERTY(EditAnywhere, Category = "Rebuild", AdvancedDisplay, meta = (ClampMin = "0", UIMax = "4"))
	int32 LODIndex = 0;

	virtual FText GetDisplayName() const override;

protected:
	virtual void Generate(FMazeGrid& InOutGrid, FRandomStream& Rng) override;
};
