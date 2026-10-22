#pragma once

#include "CoreMinimal.h"
#include "MazeOutlineShaping.generated.h"

/**
 *  The shape a passage takes where it ends in a wall. Seen from the side; the end wall is on
 *  whichever side the passage stops, and the shape is mirrored to match.
 */
UENUM(BlueprintType)
enum class EMazeDeadEndShape : uint8
{
	/** Left as it is: a square end wall. In a mix, the share of ends that stay plain. */
	Flat = 0,

	/** Closes in from above and below to a point in the middle, like an arrowhead. */
	Arrow = 1,

	/** The ceiling slopes down to the floor. The floor stays flat to the end. */
	SlantDown = 2,

	/** The floor rises to the ceiling, in steps — a climb, not a walk. */
	SlantUp = 3,

	/** A half-circle end. */
	Round = 4,

	/** A half-hexagon: corners cut at 45 degrees, a flat face half the passage high. */
	Polygon = 5
};

/** One shape in the mix, and how often it comes up relative to the others. */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeDeadEndShapeChoice
{
	GENERATED_BODY()

	FMazeDeadEndShapeChoice() = default;
	FMazeDeadEndShapeChoice(EMazeDeadEndShape InShape, float InWeight) : Shape(InShape), Weight(InWeight) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dead End")
	EMazeDeadEndShape Shape = EMazeDeadEndShape::Arrow;

	/** Relative weight. 2 comes up twice as often as 1; 0 never. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dead End", meta = (ClampMin = "0"))
	float Weight = 1.0f;
};
