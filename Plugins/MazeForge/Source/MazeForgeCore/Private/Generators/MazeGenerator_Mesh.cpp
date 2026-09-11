#include "Generators/MazeGenerator_Mesh.h"

#include "Data/MazeGrid.h"
#include "Engine/StaticMesh.h"
#include "Generators/MazeLayout2D.h"
#include "MazeForgeCore.h"
#include "StaticMeshResources.h"

#if WITH_EDITOR
#include "MeshAttributes.h"
#include "MeshDescription.h"
#endif

#define LOCTEXT_NAMESPACE "MazeForge"

FText UMazeGenerator_Mesh::GetDisplayName() const
{
	return LOCTEXT("GeneratorMesh", "Static Mesh");
}

void UMazeGenerator_Mesh::Generate(FMazeGrid& InOutGrid, FRandomStream& Rng)
{
	UStaticMesh* Source = Mesh.LoadSynchronous();
	if (!Source)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Mesh: no mesh set."));
		return;
	}

	// --- Collecting the triangles projected onto the plane of the maze.
	//
	// The depth is discarded straight away: the maze is flat, and the volume along Y
	// will be grown later through the grid's depth profile.

	TArray<FVector2D> Corners;
	FString SourceKind;

#if WITH_EDITOR
	// The mesh's source description, not LODResources.
	//
	// On a mesh with Nanite enabled, LODResources[0] is a coarse fallback: on a real
	// maze of 26458 triangles it gave 4858, decimated fivefold. The silhouette built
	// from them came out as entirely different geometry.
	if (const FMeshDescription* Description = Source->GetMeshDescription(FMath::Max(0, LODIndex)))
	{
		const FMeshConstAttributes Attributes(*Description);
		const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();

		Corners.Reserve(Description->Triangles().Num() * 3);

		for (const FTriangleID TriangleID : Description->Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexID> Vertices = Description->GetTriangleVertices(TriangleID);
			if (Vertices.Num() != 3)
			{
				continue;
			}

			for (const FVertexID VertexID : Vertices)
			{
				const FVector3f Position = Positions[VertexID];
				Corners.Add(FVector2D(Position.X, Position.Z));
			}
		}

		SourceKind = TEXT("MeshDescription");
	}
#endif

	if (Corners.Num() == 0)
	{
		// The fallback path for a cooked game and for meshes with no source description.
		const FStaticMeshRenderData* RenderData = Source->GetRenderData();
		if (!RenderData || RenderData->LODResources.Num() == 0)
		{
			UE_LOG(LogMazeForge, Warning,
				TEXT("Mesh: %s has neither a source description nor render data."), *Source->GetName());
			return;
		}

		const int32 LOD = FMath::Clamp(LODIndex, 0, RenderData->LODResources.Num() - 1);
		const FStaticMeshLODResources& Resources = RenderData->LODResources[LOD];
		const FPositionVertexBuffer& Positions = Resources.VertexBuffers.PositionVertexBuffer;
		const uint32 VertexCount = Positions.GetNumVertices();

		TArray<uint32> Indices;
		Resources.IndexBuffer.GetCopy(Indices);

		for (int32 At = 0; At + 2 < Indices.Num(); At += 3)
		{
			if (Indices[At] >= VertexCount || Indices[At + 1] >= VertexCount
				|| Indices[At + 2] >= VertexCount)
			{
				continue;
			}

			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const FVector3f Position = Positions.VertexPosition(Indices[At + Corner]);
				Corners.Add(FVector2D(Position.X, Position.Z));
			}
		}

		SourceKind = TEXT("LODResources (fallback)");

		UE_LOG(LogMazeForge, Warning,
			TEXT("Mesh: the source description of %s is unavailable, reading LODResources. ")
			TEXT("On a Nanite mesh that is a decimated fallback, the maze will come out coarser than the original."),
			*Source->GetName());
	}

	const int32 Triangles = Corners.Num() / 3;
	if (Triangles == 0)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Mesh: %s has no triangles."), *Source->GetName());
		return;
	}

	const double CellX = FMath::Max(1.0, VoxelSize.X);
	const double CellZ = FMath::Max(1.0, VoxelSize.Z);

	// --- Bounds from the projection, not from GetBoundingBox().
	//
	// Real exports sometimes have junk sticking out along the depth axis; it would stretch
	// the bounds and shift the whole grid by a cell. In the plane of the maze it is absent.

	FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D Max(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());

	for (const FVector2D& Corner : Corners)
	{
		Min.X = FMath::Min(Min.X, Corner.X);
		Min.Y = FMath::Min(Min.Y, Corner.Y);
		Max.X = FMath::Max(Max.X, Corner.X);
		Max.Y = FMath::Max(Max.Y, Corner.Y);
	}

	const FIntPoint Size(
		FMath::Max(1, FMath::RoundToInt((Max.X - Min.X) / CellX)),
		FMath::Max(1, FMath::RoundToInt((Max.Y - Min.Y) / CellZ)));

	constexpr int64 MaxCells = 16ll * 1024 * 1024;
	if (static_cast<int64>(Size.X) * Size.Y > MaxCells)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Mesh: %s at a cube of %.0f gives a %d x %d grid. Increase Voxel Size."),
			*Source->GetName(), CellX, Size.X, Size.Y);
		return;
	}

	// --- Rasterisation: the cell centre against the projection of a triangle.

	FMazeLayout2D Layout(Size);
	Layout.Fill(EMazeCellType::Empty);

	int32 Solid = 0;

	for (int32 Index = 0; Index < Triangles; ++Index)
	{
		const FVector2D& A = Corners[Index * 3];
		const FVector2D& B = Corners[Index * 3 + 1];
		const FVector2D& C = Corners[Index * 3 + 2];

		// Twice the signed area. The side faces of a wall project into a segment, their
		// area is zero — they mark nothing, and that is correct: the footprint of the
		// maze is defined only by the caps facing the camera.
		const double Area2 = (B.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (B.Y - A.Y);
		if (FMath::Abs(Area2) < UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}

		const int32 X0 = FMath::Clamp(
			FMath::FloorToInt((FMath::Min3(A.X, B.X, C.X) - Min.X) / CellX), 0, Size.X - 1);
		const int32 X1 = FMath::Clamp(
			FMath::CeilToInt((FMath::Max3(A.X, B.X, C.X) - Min.X) / CellX), 0, Size.X);
		const int32 Z0 = FMath::Clamp(
			FMath::FloorToInt((FMath::Min3(A.Y, B.Y, C.Y) - Min.Y) / CellZ), 0, Size.Y - 1);
		const int32 Z1 = FMath::Clamp(
			FMath::CeilToInt((FMath::Max3(A.Y, B.Y, C.Y) - Min.Y) / CellZ), 0, Size.Y);

		for (int32 Z = Z0; Z < Z1; ++Z)
		{
			const double CenterZ = Min.Y + (Z + 0.5) * CellZ;

			for (int32 X = X0; X < X1; ++X)
			{
				if (Layout.Get(X, Z) == SolidType)
				{
					continue;
				}

				const double CenterX = Min.X + (X + 0.5) * CellX;

				// Barycentric signs. We divide by the signed area, so the winding order
				// of the triangle does not matter.
				const double W0 = ((B.X - A.X) * (CenterZ - A.Y) - (CenterX - A.X) * (B.Y - A.Y)) / Area2;
				const double W1 = ((C.X - B.X) * (CenterZ - B.Y) - (CenterX - B.X) * (C.Y - B.Y)) / Area2;
				const double W2 = ((A.X - C.X) * (CenterZ - C.Y) - (CenterX - C.X) * (A.Y - C.Y)) / Area2;

				if (W0 >= 0.0 && W1 >= 0.0 && W2 >= 0.0)
				{
					Layout.Set(X, Z, SolidType);
					++Solid;
				}
			}
		}
	}

	// --- Filling in the unreachable.

	int32 FilledCells = 0;
	int32 FilledPockets = 0;

	if (bFillUnreachable)
	{
		const int32 AgentHeight = FMath::Max(1, AgentHeightCells);

		// Entry from above: the topmost cell the player fits into, closer to the middle.
		FIntPoint Entry(INDEX_NONE, INDEX_NONE);
		for (int32 Z = Size.Y - 1; Z >= 0 && Entry.X == INDEX_NONE; --Z)
		{
			int32 BestX = INDEX_NONE;
			int32 BestDistance = MAX_int32;

			for (int32 X = 0; X < Size.X; ++X)
			{
				if (!Layout.CanStand(X, Z, AgentHeight))
				{
					continue;
				}

				const int32 Distance = FMath::Abs(X - Size.X / 2);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					BestX = X;
				}
			}

			if (BestX != INDEX_NONE)
			{
				Entry = FIntPoint(BestX, Z);
			}
		}

		if (Entry.X == INDEX_NONE)
		{
			UE_LOG(LogMazeForge, Warning,
				TEXT("Mesh: no place was found that a player of height %d fits into. ")
				TEXT("The fill was skipped."), AgentHeight);
		}
		else
		{
			TBitArray<> Standable;
			TBitArray<> Occupied;
			Layout.FloodFill(Entry, AgentHeight, Standable);
			Layout.BuildOccupied(Standable, AgentHeight, Occupied);

			TArray<FIntPoint> Pockets;
			TArray<int32> PocketSizes;
			Layout.FindIslands(Standable, AgentHeight, Pockets, PocketSizes);

			for (const FIntPoint& Pocket : Pockets)
			{
				// The limiter is needed: without it the fill would leak through a one-cell
				// gap straight into the living maze. To the fill that gap is passable, to
				// the player it is not.
				const int32 Filled = Layout.FloodSolidBounded(Pocket, Occupied);
				if (Filled > 0)
				{
					FilledCells += Filled;
					++FilledPockets;
				}
			}
		}
	}

	// --- The grid and the volume along the depth.

	InOutGrid.CellSize = FVector(CellX, FMath::Max(1.0, VoxelSize.Y), CellZ);
	InOutGrid.SizeXZ = Size;

	// We deliberately do not touch the depth profile: the volume along Y is what the
	// designer set up in the grid, not what was in the flat source.
	MazeWriteLayoutToGrid(Layout, InOutGrid);

	UE_LOG(LogMazeForge, Log,
		TEXT("Mesh: %s, source %s, %d triangles -> grid %d x %d, ")
		TEXT("%d solid, %d cavities filled over %d cells, depth %d (BG %d / Play %d / FG %d)."),
		*Source->GetName(), *SourceKind, Triangles, Size.X, Size.Y, Solid,
		FilledPockets, FilledCells, InOutGrid.DepthCells(),
		InOutGrid.Depth.BackgroundCells, InOutGrid.Depth.PlayCells,
		InOutGrid.Depth.ForegroundCells);
}

#undef LOCTEXT_NAMESPACE
