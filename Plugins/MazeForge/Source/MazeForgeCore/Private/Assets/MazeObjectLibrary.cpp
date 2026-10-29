#include "Assets/MazeObjectLibrary.h"

#include "MazeForgeCore.h"

const FMazeObjectType* UMazeObjectLibrary::FindType(FName TypeId) const
{
	if (TypeId.IsNone())
	{
		return nullptr;
	}

	return Types.FindByPredicate([TypeId](const FMazeObjectType& Type)
	{
		return Type.TypeId == TypeId;
	});
}

FName UMazeObjectLibrary::PickVariant(FName TypeId, uint32 Hash) const
{
	const FMazeObjectType* Wanted = FindType(TypeId);
	if (!Wanted || !Wanted->bRandomVariant)
	{
		return TypeId;
	}

	// The same role and not merely the same category: a ladder must not turn into its own
	// platform, and a door must not turn into the key that opens it.
	const bool bWantedTop = IsTopPiece(TypeId);

	TArray<FName, TInlineAllocator<8>> Candidates;
	for (const FMazeObjectType& Type : Types)
	{
		if (!Type.TypeId.IsNone()
			&& Type.Category == Wanted->Category
			&& Type.LockRole == Wanted->LockRole
			&& Type.LiftRole == Wanted->LiftRole
			&& Type.bRope == Wanted->bRope
			&& Type.bSlidingDoor == Wanted->bSlidingDoor
			&& Type.TransitionRole == Wanted->TransitionRole
			&& IsTopPiece(Type.TypeId) == bWantedTop)
		{
			Candidates.Add(Type.TypeId);
		}
	}

	return Candidates.Num() > 0 ? Candidates[Hash % static_cast<uint32>(Candidates.Num())] : TypeId;
}

bool UMazeObjectLibrary::IsTopPiece(FName TypeId) const
{
	return !TypeId.IsNone() && Types.ContainsByPredicate([TypeId](const FMazeObjectType& Type)
	{
		return Type.TopPieceType == TypeId;
	});
}

void UMazeObjectLibrary::GetTypeIds(TArray<FName>& OutIds) const
{
	OutIds.Reset(Types.Num());

	for (const FMazeObjectType& Type : Types)
	{
		if (!Type.TypeId.IsNone())
		{
			OutIds.Add(Type.TypeId);
		}
	}
}

void UMazeObjectLibrary::FindBrokenIds(TArray<FName>& OutDuplicates, int32& OutUnnamed) const
{
	OutDuplicates.Reset();
	OutUnnamed = 0;

	TSet<FName> Seen;
	Seen.Reserve(Types.Num());

	for (const FMazeObjectType& Type : Types)
	{
		if (Type.TypeId.IsNone())
		{
			++OutUnnamed;
			continue;
		}

		bool bAlready = false;
		Seen.Add(Type.TypeId, &bAlready);

		if (bAlready)
		{
			OutDuplicates.AddUnique(Type.TypeId);
		}
	}
}

void UMazeObjectLibrary::ReportBrokenIds() const
{
	TArray<FName> Duplicates;
	int32 Unnamed = 0;
	FindBrokenIds(Duplicates, Unnamed);

	// Said out loud, because neither failure shows itself where it happens. Placements refer to
	// types by name: a duplicate id means some of them resolve to the wrong object and the level
	// simply comes out wrong, with nothing anywhere to say why.
	for (const FName& Duplicate : Duplicates)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("%s: type id '%s' is used more than once. Placements using it will resolve to "
			     "whichever entry comes first."),
			*GetName(), *Duplicate.ToString());
	}

	if (Unnamed > 0)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("%s: %d object types have no Type Id and cannot be placed."),
			*GetName(), Unnamed);
	}
}

#if WITH_EDITOR
void UMazeObjectLibrary::PostLoad()
{
	Super::PostLoad();
	ReportBrokenIds();
}

void UMazeObjectLibrary::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ReportBrokenIds();
}
#endif
