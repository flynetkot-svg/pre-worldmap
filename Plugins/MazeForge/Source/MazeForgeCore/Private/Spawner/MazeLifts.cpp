#include "Spawner/MazeLifts.h"

#include "Assets/MazeObjectLibrary.h"

namespace MazeLifts
{
	bool IsLift(const UMazeObjectLibrary* Library, const FMazePlacement& Placement)
	{
		const FMazeObjectType* Type = Library ? Library->FindType(Placement.TypeId) : nullptr;
		return Type && Type->LiftRole == EMazeLiftRole::Platform;
	}

	bool IsCall(const UMazeObjectLibrary* Library, const FMazePlacement& Placement)
	{
		const FMazeObjectType* Type = Library ? Library->FindType(Placement.TypeId) : nullptr;
		return Type && Type->LiftRole == EMazeLiftRole::Call;
	}

	FName ResolveId(const FMazePlacement& Lift)
	{
		return Lift.LiftId.IsNone()
			? FName(*FString::Printf(TEXT("Lift_%d_%d"), Lift.CellXZ.X, Lift.CellXZ.Y))
			: Lift.LiftId;
	}

	bool BindCall(const TArray<FMazePlacement>& Placements, const UMazeObjectLibrary* Library,
	              const FMazePlacement& Call, FMazeLiftBinding& Out, int32 MaxOffCells)
	{
		const int32 Level = Call.CellXZ.Y;
		int64 Best = TNumericLimits<int64>::Max();

		for (const FMazePlacement& Lift : Placements)
		{
			if (!IsLift(Library, Lift))
			{
				continue;
			}

			const int32 Levels[2] = { Lift.CellXZ.Y, Lift.CellXZ.Y + FMath::Max(0, Lift.ClimbCells) };
			const int32 Stop = FMath::Abs(Levels[0] - Level) <= FMath::Abs(Levels[1] - Level) ? 0 : 1;
			const int32 Off = FMath::Abs(Levels[Stop] - Level);

			int64 Score = 0;
			if (!Call.LiftId.IsNone())
			{
				// Named: only that lift, at whichever of its stops is nearer.
				if (ResolveId(Lift) != Call.LiftId)
				{
					continue;
				}
			}
			else
			{
				// Found: a stop at the terminal's level, the nearest such lift along the level.
				if (Off > MaxOffCells)
				{
					continue;
				}
				Score = static_cast<int64>(FMath::Abs(Lift.CellXZ.X - Call.CellXZ.X)) * 4 + Off;
			}

			if (Score < Best)
			{
				Best = Score;
				Out.LiftId = ResolveId(Lift);
				Out.Stop = Stop;
			}
		}

		return Best != TNumericLimits<int64>::Max();
	}
}
