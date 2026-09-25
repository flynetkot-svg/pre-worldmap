#include "Spawner/MazePlacementRules.h"

#include "Data/MazeGrid.h"

namespace MazePlacement
{
	namespace
	{
		/** The four anchors in the order intent runs: standing beats hanging beats leaning. */
		const EMazeAnchorKind AnchorOrder[4] = {
			EMazeAnchorKind::Floor,
			EMazeAnchorKind::Ceiling,
			EMazeAnchorKind::Wall,
			EMazeAnchorKind::Free
		};

		FIntPoint SafeFootprint(const FMazeObjectType& Type)
		{
			return FIntPoint(FMath::Max(1, Type.FootprintCells.X),
			                 FMath::Max(1, Type.FootprintCells.Y));
		}
	}

	bool Fits(const FMazeGrid& Grid, const FMazeObjectType& Type,
	          const FIntPoint& CellXZ, int32 SliceY, EMazeAnchorKind Anchor)
	{
		if (!Type.AllowsAnchor(Anchor))
		{
			return false;
		}

		const FIntPoint Size = SafeFootprint(Type);

		// The space the object needs comes first, whatever it is anchored to. A crate wedged
		// into a wall is not "attached to the floor", it is inside the wall.
		if (!Grid.IsBoxPassable(CellXZ, Size, SliceY))
		{
			return false;
		}

		const int32 MinX = CellXZ.X;
		const int32 MaxX = CellXZ.X + Size.X;
		const int32 MinZ = CellXZ.Y;
		const int32 MaxZ = CellXZ.Y + Size.Y;

		switch (Anchor)
		{
		case EMazeAnchorKind::Floor:
			return Grid.IsRowSolid(MinX, MaxX, MinZ - 1, SliceY);

		case EMazeAnchorKind::Ceiling:
			return Grid.IsRowSolid(MinX, MaxX, MaxZ, SliceY);

		case EMazeAnchorKind::Wall:
			// Either side will do. Which side it turned out to be is what ResolveRotation
			// looks at again when the object has to face away from it.
			return Grid.IsColumnSolid(MinX - 1, MinZ, MaxZ, SliceY)
				|| Grid.IsColumnSolid(MaxX, MinZ, MaxZ, SliceY);

		case EMazeAnchorKind::Free:
		default:
			return true;
		}
	}

	bool FindAnchor(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                const FIntPoint& CellXZ, int32 SliceY, EMazeAnchorKind& OutAnchor)
	{
		for (const EMazeAnchorKind Candidate : AnchorOrder)
		{
			if (Fits(Grid, Type, CellXZ, SliceY, Candidate))
			{
				OutAnchor = Candidate;
				return true;
			}
		}

		return false;
	}

	FString DescribeMisfit(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                       const FIntPoint& CellXZ, int32 SliceY)
	{
		EMazeAnchorKind Unused;
		if (FindAnchor(Grid, Type, CellXZ, SliceY, Unused))
		{
			return FString();
		}

		if (Type.AllowedAnchors == 0)
		{
			return TEXT("the type allows no anchors at all — tick at least one under Allowed Anchors");
		}

		const FIntPoint Size = SafeFootprint(Type);

		// Checked in the order a person would look: is it even on the map, is the space free,
		// and only then what it was supposed to hold on to.
		for (int32 X = CellXZ.X; X < CellXZ.X + Size.X; ++X)
		{
			for (int32 Z = CellXZ.Y; Z < CellXZ.Y + Size.Y; ++Z)
			{
				if (!Grid.IsInside(FIntVector(X, SliceY, Z)))
				{
					return TEXT("part of the footprint is outside the grid");
				}
			}
		}

		for (int32 X = CellXZ.X; X < CellXZ.X + Size.X; ++X)
		{
			for (int32 Z = CellXZ.Y; Z < CellXZ.Y + Size.Y; ++Z)
			{
				if (Grid.IsSolid(FIntVector(X, SliceY, Z)))
				{
					return FString::Printf(
						TEXT("X %d Z %d is mass — an object stands in the empty cell NEXT to the "
						     "floor, not in the floor itself"), X, Z);
				}

				// Named apart from mass, because nothing was ever painted here and the cell
				// looks empty by every means a designer has of checking. It is the world
				// border: the export builds it as real geometry, so an object standing here
				// is inside a wall that is not in the drawing.
				if (Grid.IsBorderCell(FIntVector(X, SliceY, Z)))
				{
					return FString::Printf(
						TEXT("X %d Z %d is inside the world border — it is built as mass even "
						     "though nothing is drawn there. Move inwards, or open that side "
						     "under Grid -> Borders"), X, Z);
				}
			}
		}

		// The space is free, so what is missing is something to hold on to. Only the anchors the
		// type actually allows are named: telling someone there is no ceiling above a crate that
		// was never allowed to hang is noise.
		TArray<FString> Missing;

		if (Type.AllowsAnchor(EMazeAnchorKind::Floor))
		{
			Missing.Add(TEXT("no mass in the row below"));
		}
		if (Type.AllowsAnchor(EMazeAnchorKind::Ceiling))
		{
			Missing.Add(TEXT("no mass in the row above"));
		}
		if (Type.AllowsAnchor(EMazeAnchorKind::Wall))
		{
			Missing.Add(TEXT("no mass on either side"));
		}

		// Free fits anywhere, so if it were allowed we would never have got here.
		return Missing.Num() > 0 ? FString::Join(Missing, TEXT("; ")) : TEXT("nothing to attach to");
	}

	EMazeAnchorKind PreferredAnchor(const FMazeObjectType& Type)
	{
		for (const EMazeAnchorKind Candidate : AnchorOrder)
		{
			if (Type.AllowsAnchor(Candidate))
			{
				return Candidate;
			}
		}

		// A type that allows nothing is a data error, not a placement problem. Floor is the
		// least surprising thing to record while the panel complains about the type.
		return EMazeAnchorKind::Floor;
	}

	FRotator ResolveRotation(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                         const FIntPoint& CellXZ, int32 SliceY,
	                         EMazeAnchorKind Anchor, int32 Seed)
	{
		switch (Type.FacingMode)
		{
		case EMazeFacingMode::RandomX:
		{
			// Seeded on the cell as well as the seed, so the same maze rebuilt with the same
			// seed puts the same crate the same way round. A world that differs between two
			// identical builds is a world nobody can test against.
			FRandomStream Stream(Seed ^ (CellXZ.X * 73856093) ^ (CellXZ.Y * 19349663));

			FRotator Rotation = Type.FixedRotation;
			Rotation.Yaw += Stream.FRand() < 0.5f ? 0.0f : 180.0f;
			return Rotation;
		}

		case EMazeFacingMode::AwayFromWall:
		{
			if (Anchor == EMazeAnchorKind::Wall)
			{
				const FIntPoint Size = SafeFootprint(Type);
				const bool bWallOnLeft = Grid.IsColumnSolid(
					CellXZ.X - 1, CellXZ.Y, CellXZ.Y + Size.Y, SliceY);

				FRotator Rotation = Type.FixedRotation;
				Rotation.Yaw += bWallOnLeft ? 0.0f : 180.0f;
				return Rotation;
			}

			// Not on a wall, so there is nothing to face away from. The fixed angle is the
			// honest answer; inventing a direction here would be a guess wearing a rule's coat.
			return Type.FixedRotation;
		}

		case EMazeFacingMode::Fixed:
		default:
			return Type.FixedRotation;
		}
	}

	int32 BandSliceY(const FMazeGrid& Grid, EMazeDepthBand Band)
	{
		const FMazeDepthProfile& Depth = Grid.Depth;
		const int32 Last = FMath::Max(0, Depth.TotalCells() - 1);

		switch (Band)
		{
		case EMazeDepthBand::Background:
			return FMath::Clamp(Depth.BackgroundCells / 2, 0, Last);

		case EMazeDepthBand::Foreground:
			return FMath::Clamp(Depth.PlayEndCell() + Depth.ForegroundCells / 2, 0, Last);

		case EMazeDepthBand::Play:
		default:
			// The play plane, which is where the maze itself is drawn. An object in the play
			// band shares the player's slice by definition.
			return Depth.PlaneCellY();
		}
	}

	FVector WorldLocation(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                      const FMazePlacement& Placement)
	{
		const FIntPoint Size = SafeFootprint(Type);
		const int32 SliceY = BandSliceY(Grid, Placement.Band);

		// The whole footprint, not its first cell. A two-cell crate placed by its bottom-left
		// corner would otherwise stand half a cell off from where it was drawn.
		const FBox Box =
			Grid.GetCellBounds(FIntVector(Placement.CellXZ.X, SliceY, Placement.CellXZ.Y))
			+ Grid.GetCellBounds(FIntVector(Placement.CellXZ.X + Size.X - 1, SliceY,
				Placement.CellXZ.Y + Size.Y - 1));

		// The origin goes ON the surface the object is anchored by, not in the middle of the box.
		// Which surface that is, is one question asked in two places — here, and by the export
		// when it snaps the spawned actor against it — so it is answered once, in ContactFace.
		FVector Location = Box.GetCenter();

		switch (ContactFace(Grid, Type, Placement))
		{
		case EMazeContactFace::MinZ: Location.Z = Box.Min.Z; break;
		case EMazeContactFace::MaxZ: Location.Z = Box.Max.Z; break;
		case EMazeContactFace::MinX: Location.X = Box.Min.X; break;
		case EMazeContactFace::MaxX: Location.X = Box.Max.X; break;

		case EMazeContactFace::None:
		default:
			// Nothing to touch, so the middle of the space it was given is the honest answer.
			break;
		}

		return Location + Type.Offset + Placement.Offset;
	}

	EMazeContactFace ContactFace(const FMazeGrid& Grid, const FMazeObjectType& Type,
	                             const FMazePlacement& Placement)
	{
		switch (Placement.Anchor)
		{
		case EMazeAnchorKind::Floor:
			return EMazeContactFace::MinZ;

		case EMazeAnchorKind::Ceiling:
			return EMazeContactFace::MaxZ;

		case EMazeAnchorKind::Wall:
		{
			// Whichever side actually has the mass. Asked again rather than stored: the
			// placement records that it leans on a wall, never which wall, and re-deriving it
			// keeps that fact in one place — the same place ResolveRotation asks.
			const FIntPoint Size = SafeFootprint(Type);
			const int32 SliceY = BandSliceY(Grid, Placement.Band);

			const bool bWallOnLeft = Grid.IsColumnSolid(
				Placement.CellXZ.X - 1, Placement.CellXZ.Y, Placement.CellXZ.Y + Size.Y, SliceY);

			return bWallOnLeft ? EMazeContactFace::MinX : EMazeContactFace::MaxX;
		}

		case EMazeAnchorKind::Free:
		default:
			return EMazeContactFace::None;
		}
	}

	FVector ContactSnapDelta(const FBox& ActorBounds, const EMazeContactFace Face,
	                         const FVector& SurfacePoint)
	{
		if (Face == EMazeContactFace::None || !ActorBounds.IsValid)
		{
			return FVector::ZeroVector;
		}

		// The move that puts the actor's own edge on the surface, whatever its pivot happens to
		// be. This is the piece the placement rules cannot know on their own: where a mesh sits
		// relative to its origin is a fact about the asset, and the asset lives in the game
		// module. Guessing it — "a crate's pivot is at its base, a lamp's at its top" — held for
		// exactly as long as every prop was modelled to that convention, and then a lamp with a
		// base pivot grew straight up into the ceiling.
		FVector Delta = FVector::ZeroVector;

		switch (Face)
		{
		case EMazeContactFace::MinZ: Delta.Z = SurfacePoint.Z - ActorBounds.Min.Z; break;
		case EMazeContactFace::MaxZ: Delta.Z = SurfacePoint.Z - ActorBounds.Max.Z; break;
		case EMazeContactFace::MinX: Delta.X = SurfacePoint.X - ActorBounds.Min.X; break;
		case EMazeContactFace::MaxX: Delta.X = SurfacePoint.X - ActorBounds.Max.X; break;
		default: break;
		}

		return Delta;
	}
}
