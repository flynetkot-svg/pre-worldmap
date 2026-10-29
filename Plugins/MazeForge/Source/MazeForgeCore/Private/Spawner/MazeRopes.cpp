#include "Spawner/MazeRopes.h"

#include "Assets/MazeObjectLibrary.h"
#include "Data/MazeGrid.h"

namespace MazeRopes
{
	namespace
	{
		bool Blocked(const FMazeGrid& Grid, int32 SliceY, int32 X, int32 Z)
		{
			const FIntVector C(X, SliceY, Z);
			return !Grid.IsInside(C) || Grid.IsSolid(C) || Grid.IsBorderCell(C);
		}
	}

	bool IsRope(const UMazeObjectLibrary* Library, const FMazePlacement& Placement)
	{
		const FMazeObjectType* Type = Library ? Library->FindType(Placement.TypeId) : nullptr;
		return Type && Type->bRope;
	}

	bool PickLevel(const FMazeGrid& Grid, int32 SliceY, const FIntPoint& Cell, int32 Tolerance, int32& OutLevel)
	{
		// In mass: the first free row above it.
		if (Blocked(Grid, SliceY, Cell.X, Cell.Y))
		{
			for (int32 Up = 1; Up <= Tolerance; ++Up)
			{
				if (!Blocked(Grid, SliceY, Cell.X, Cell.Y + Up))
				{
					OutLevel = Cell.Y + Up;
					return true;
				}
			}
			return false;
		}

		// In air: the floor under it, if it is near; over a gap, the row clicked.
		for (int32 Down = 0; Down <= Tolerance; ++Down)
		{
			if (Blocked(Grid, SliceY, Cell.X, Cell.Y - Down - 1))
			{
				OutLevel = Cell.Y - Down;
				return true;
			}
		}
		OutLevel = Cell.Y;
		return true;
	}

	bool FitSpan(const FMazeGrid& Grid, int32 SliceY, int32 Level, int32 XA, int32 XB,
	             FMazeRopeSpan& Out, FString* OutReason)
	{
		auto Fail = [OutReason](const FString& Reason)
		{
			if (OutReason)
			{
				*OutReason = Reason;
			}
			return false;
		};

		const int32 Lo = FMath::Min(XA, XB);
		const int32 Hi = FMath::Max(XA, XB);

		for (int32 X = Lo; X <= Hi; ++X)
		{
			if (Blocked(Grid, SliceY, X, Level))
			{
				return Fail(FString::Printf(TEXT("mass at X %d Z %d stands in the way"), X, Level));
			}
		}

		auto Supported = [&Grid, SliceY, Level](int32 X) { return Blocked(Grid, SliceY, X, Level - 1); };

		int32 First = Lo;
		while (First <= Hi && Supported(First))
		{
			++First;
		}
		int32 Last = Hi;
		while (Last >= First && Supported(Last))
		{
			--Last;
		}
		if (First > Last)
		{
			return Fail(FString::Printf(TEXT("floor all the way along Z %d — no gap to span"), Level));
		}

		// Each end tied to something: the ledge it was trimmed off, or a wall right beside it.
		Out.bLeftLedge = First > Lo;
		Out.bRightLedge = Last < Hi;
		if (!Out.bLeftLedge && !Blocked(Grid, SliceY, First - 1, Level) && !Supported(First - 1))
		{
			return Fail(FString::Printf(TEXT("at Z %d the left end (X %d) hangs in the air — no ledge or wall beside it"), Level, First));
		}
		if (!Out.bRightLedge && !Blocked(Grid, SliceY, Last + 1, Level) && !Supported(Last + 1))
		{
			return Fail(FString::Printf(TEXT("at Z %d the right end (X %d) hangs in the air — no ledge or wall beside it"), Level, Last));
		}
		// A click right at the edge of the gap: the ledge is there all the same.
		Out.bLeftLedge = Out.bLeftLedge || (!Blocked(Grid, SliceY, First - 1, Level) && Supported(First - 1));
		Out.bRightLedge = Out.bRightLedge || (!Blocked(Grid, SliceY, Last + 1, Level) && Supported(Last + 1));

		Out.Level = Level;
		Out.FirstX = First;
		Out.LastX = Last;
		return true;
	}

	bool FindSpan(const FMazeGrid& Grid, int32 SliceY, const FIntPoint& ClickA, int32 LevelA,
	              const FIntPoint& ClickB, int32 Tolerance, FMazeRopeSpan& Out, FString* OutReason)
	{
		int32 LevelB = ClickB.Y;
		PickLevel(Grid, SliceY, ClickB, Tolerance, LevelB);

		const int32 Lo = FMath::Min3(LevelA, LevelB, FMath::Min(ClickA.Y, ClickB.Y)) - Tolerance;
		const int32 Hi = FMath::Max3(LevelA, LevelB, FMath::Max(ClickA.Y, ClickB.Y)) + Tolerance;

		int32 BestScore = MAX_int32;
		for (int32 Level = Lo; Level <= Hi; ++Level)
		{
			FMazeRopeSpan Span;
			if (!FitSpan(Grid, SliceY, Level, ClickA.X, ClickB.X, Span))
			{
				continue;
			}
			// A ledge at each end is what a rope is for; a wall will do; nearer the clicks is better.
			const int32 Missing = (Span.bLeftLedge ? 0 : 1) + (Span.bRightLedge ? 0 : 1);
			const int32 Score = Missing * 1000 + FMath::Abs(Level - LevelA) * 2 + FMath::Abs(Level - LevelB);
			if (Score < BestScore)
			{
				BestScore = Score;
				Out = Span;
			}
		}

		if (BestScore == MAX_int32)
		{
			FMazeRopeSpan Ignored;
			FitSpan(Grid, SliceY, LevelA, ClickA.X, ClickB.X, Ignored, OutReason);
			return false;
		}
		return true;
	}

	FIntRect Rect(const FMazePlacement& Rope)
	{
		return FIntRect(Rope.CellXZ, Rope.CellXZ + FIntPoint(FMath::Max(1, Rope.SpanCells), 1));
	}
}
