#include "Export/MazeMeshBuilder_Faces.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Assets/MazeBuildSettings.h"
#include "Data/MazeGrid.h"
#include "Data/MazeRoomDesc.h"
#include "Engine/StaticMesh.h"
#include "Export/MazeCollisionBuilder.h"
#include "Materials/Material.h"
#include "MazeForgeCore.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BoxElem.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "UObject/Package.h"

namespace
{
	const FName MaterialSlotName(TEXT("MazeForge"));

	/**
	 *  Short name of a cell type for the material slot.
	 *
	 *  Written out by hand rather than taken from reflection: UEnum::GetValueAsString yields
	 *  "EMazeCellType::BackWall", and a slot name with a colon in it is unpleasant to read in
	 *  the mesh editor, which is the only place these names are ever seen.
	 */
	const TCHAR* SurfaceTypeName(EMazeCellType Type)
	{
		switch (Type)
		{
		case EMazeCellType::Floor:    return TEXT("Floor");
		case EMazeCellType::BackWall: return TEXT("BackWall");
		default:                      return TEXT("Solid");
		}
	}

	/** A single quad of a single cell face. */
	struct FMazeQuad
	{
		FVector Corners[4];
		FVector2f UVs[4];
		FVector3f Normal;

		/** Index into the surface list gathered below: which material slot this quad belongs to. */
		int32 Surface = 0;
	};

	/**
	 *  One material slot of the produced mesh.
	 *
	 *  Surfaces are gathered while walking the cells, not known up front: a room may hold only
	 *  maze mass, only back wall, or any mixture, and creating slots for materials nobody used
	 *  would leave empty sections in every mesh.
	 */
	struct FMazeSurface
	{
		EMazeCellType Type = EMazeCellType::Solid;
		uint8 Variant = 0;
		UMaterialInterface* Material = nullptr;
		FName SlotName;
	};

	/**
	 *  Six faces. The corners are listed so that (V1-V0) x (V2-V0) points outwards.
	 *
	 *  Careful: this is NOT the winding order for MeshDescription. The engine computes a
	 *  triangle normal as Cross(V2-V0, V1-V0) — the reverse product, because the coordinate
	 *  system is left-handed and the winding of front faces is counter-clockwise.
	 *  That is why the corners are fed to polygon creation in reverse order, see below.
	 */
	void MakeQuad(const FBox& Cell, int32 FaceIndex, FMazeQuad& OutQuad)
	{
		const FVector& Lo = Cell.Min;
		const FVector& Hi = Cell.Max;

		switch (FaceIndex)
		{
		case 0: // +X
			OutQuad.Normal = FVector3f(1.f, 0.f, 0.f);
			OutQuad.Corners[0] = FVector(Hi.X, Lo.Y, Lo.Z);
			OutQuad.Corners[1] = FVector(Hi.X, Hi.Y, Lo.Z);
			OutQuad.Corners[2] = FVector(Hi.X, Hi.Y, Hi.Z);
			OutQuad.Corners[3] = FVector(Hi.X, Lo.Y, Hi.Z);
			break;
		case 1: // -X
			OutQuad.Normal = FVector3f(-1.f, 0.f, 0.f);
			OutQuad.Corners[0] = FVector(Lo.X, Hi.Y, Lo.Z);
			OutQuad.Corners[1] = FVector(Lo.X, Lo.Y, Lo.Z);
			OutQuad.Corners[2] = FVector(Lo.X, Lo.Y, Hi.Z);
			OutQuad.Corners[3] = FVector(Lo.X, Hi.Y, Hi.Z);
			break;
		case 2: // +Y
			OutQuad.Normal = FVector3f(0.f, 1.f, 0.f);
			OutQuad.Corners[0] = FVector(Hi.X, Hi.Y, Lo.Z);
			OutQuad.Corners[1] = FVector(Lo.X, Hi.Y, Lo.Z);
			OutQuad.Corners[2] = FVector(Lo.X, Hi.Y, Hi.Z);
			OutQuad.Corners[3] = FVector(Hi.X, Hi.Y, Hi.Z);
			break;
		case 3: // -Y
			OutQuad.Normal = FVector3f(0.f, -1.f, 0.f);
			OutQuad.Corners[0] = FVector(Lo.X, Lo.Y, Lo.Z);
			OutQuad.Corners[1] = FVector(Hi.X, Lo.Y, Lo.Z);
			OutQuad.Corners[2] = FVector(Hi.X, Lo.Y, Hi.Z);
			OutQuad.Corners[3] = FVector(Lo.X, Lo.Y, Hi.Z);
			break;
		case 4: // +Z
			OutQuad.Normal = FVector3f(0.f, 0.f, 1.f);
			OutQuad.Corners[0] = FVector(Lo.X, Lo.Y, Hi.Z);
			OutQuad.Corners[1] = FVector(Hi.X, Lo.Y, Hi.Z);
			OutQuad.Corners[2] = FVector(Hi.X, Hi.Y, Hi.Z);
			OutQuad.Corners[3] = FVector(Lo.X, Hi.Y, Hi.Z);
			break;
		default: // -Z
			OutQuad.Normal = FVector3f(0.f, 0.f, -1.f);
			OutQuad.Corners[0] = FVector(Hi.X, Lo.Y, Lo.Z);
			OutQuad.Corners[1] = FVector(Lo.X, Lo.Y, Lo.Z);
			OutQuad.Corners[2] = FVector(Lo.X, Hi.Y, Lo.Z);
			OutQuad.Corners[3] = FVector(Hi.X, Hi.Y, Lo.Z);
			break;
		}

		// A planar unwrap along the two axes orthogonal to the normal: one cell is one tile.
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector& C = OutQuad.Corners[Index];
			const float U = (FaceIndex < 2) ? static_cast<float>(C.Y) : static_cast<float>(C.X);
			const float V = (FaceIndex < 4) ? static_cast<float>(C.Z) : static_cast<float>(C.Y);
			OutQuad.UVs[Index] = FVector2f(U / 100.f, -V / 100.f);
		}
	}
}

bool FMazeMeshBuilder_Faces::Build(const FMazeBakeRequest& Request, FMazeBakeResult& OutResult)
{
	if (!Request.Grid || !Request.Room)
	{
		OutResult.bFailed = true;
		return false;
	}

	const FMazeGrid& Grid = *Request.Grid;
	const FMazeRoomDesc& Room = *Request.Room;

	// With band splitting off the caller bakes a single mesh and labels it Play. That request
	// means "everything", not "the play band only" — read literally it would silently drop both
	// decor bands, and with them the painted back wall, which is exactly the geometry a designer
	// would then go looking for in vain.
	const bool bSingleMesh = Request.Settings && !Request.Settings->bSplitByDepthBand;

	int32 BandMinY = 0;
	int32 BandMaxY = Grid.DepthCells();

	if (!bSingleMesh)
	{
		switch (Request.Band)
		{
		case EMazeDepthBand::Background: BandMinY = 0;                          BandMaxY = Grid.Depth.PlayStartCell(); break;
		case EMazeDepthBand::Play:       BandMinY = Grid.Depth.PlayStartCell(); BandMaxY = Grid.Depth.PlayEndCell();   break;
		default:                         BandMinY = Grid.Depth.PlayEndCell();   BandMaxY = Grid.DepthCells();          break;
		}
	}

	const bool bCullFarFaces = Request.Settings && Request.Settings->bCullFarBoundaryFaces;

	// The pivot sits at the minimum corner of the room: that way the mesh is placed into the
	// level with no offsets, and the seams of neighbouring rooms line up exactly on cell borders.
	const FVector Origin = Grid.GetBoxWorldBounds(Room.MinXZ, Room.MaxXZ).Min;

	static const FIntVector FaceOffsets[6] = {
		FIntVector( 1,  0,  0), FIntVector(-1,  0,  0),
		FIntVector( 0,  1,  0), FIntVector( 0, -1,  0),
		FIntVector( 0,  0,  1), FIntVector( 0,  0, -1)
	};

	TArray<FMazeQuad> Quads;
	TArray<FMazeSurface> Surfaces;

	// Derived geometry — world borders and the backdrop — has no cell of its own in the grid,
	// so it falls back to the Solid surface. That is deliberate: it is the same mass of the maze,
	// only produced by a rule instead of a brush stroke.
	auto SurfaceForCell = [&Surfaces, &Grid, &Request](const FIntVector& Cell) -> int32
	{
		EMazeCellType Type = EMazeCellType::Solid;
		uint8 Variant = 0;

		if (const FMazeCell* Painted = Grid.Cells.Find(Cell))
		{
			if (Painted->HasGeometry())
			{
				Type = Painted->Type;
				Variant = Painted->PaletteIndex;
			}
		}

		const int32 Existing = Surfaces.IndexOfByPredicate(
			[Type, Variant](const FMazeSurface& Candidate)
			{
				return Candidate.Type == Type && Candidate.Variant == Variant;
			});

		if (Existing != INDEX_NONE)
		{
			return Existing;
		}

		FMazeSurface Surface;
		Surface.Type = Type;
		Surface.Variant = Variant;
		Surface.SlotName = FName(*FString::Printf(TEXT("%s_%s_%d"),
			*MaterialSlotName.ToString(), SurfaceTypeName(Type), Variant));

		if (Request.Settings)
		{
			Surface.Material = Request.Settings->ResolveMaterial(Type, Variant).LoadSynchronous();
		}

		return Surfaces.Add(Surface);
	};

	for (int32 X = Room.MinXZ.X; X < Room.MaxXZ.X; ++X)
	{
		for (int32 Z = Room.MinXZ.Y; Z < Room.MaxXZ.Y; ++Z)
		{
			for (int32 Y = BandMinY; Y < BandMaxY; ++Y)
			{
				const FIntVector Cell(X, Y, Z);
				if (!Grid.IsSolidForGeometry(Cell))
				{
					continue;
				}

				++OutResult.SourceCells;

				if (Grid.IsFullyEnclosed(Cell))
				{
					++OutResult.CulledCells;
					continue;
				}

				const FBox CellBounds = Grid.GetCellBounds(Cell);
				const int32 Surface = SurfaceForCell(Cell);

				for (int32 Face = 0; Face < 6; ++Face)
				{
					const FIntVector Neighbour = Cell + FaceOffsets[Face];

					// A face between two solids is never visible.
					if (Grid.IsInside(Neighbour) && Grid.IsSolidForGeometry(Neighbour))
					{
						continue;
					}

					// The far face of the world can be skipped: the camera sits at +Y and looks
					// towards −Y, so that side is never seen in game. But by default we do build
					// it — a closed mesh matters more than one saved layer of quads.
					if (bCullFarFaces && Face == 3 && Neighbour.Y < 0)
					{
						continue;
					}

					FMazeQuad Quad;
					MakeQuad(CellBounds, Face, Quad);
					Quad.Surface = Surface;
					Quads.Add(Quad);
				}
			}
		}
	}

	if (Quads.Num() == 0)
	{
		// The one honest "nothing here": a band with no visible surface. bFailed stays clear.
		return false;
	}

	OutResult.Quads = Quads.Num();

	// ------------------------------------------------------------------ asset

	UPackage* Package = CreatePackage(*Request.PackageName);
	if (!Package)
	{
		// Almost always a bad Mesh Package Root: no leading slash, or an illegal character.
		UE_LOG(LogMazeForge, Error, TEXT("Bake: cannot create package %s — check Mesh Package Root."),
			*Request.PackageName);
		OutResult.bFailed = true;
		return false;
	}
	Package->FullyLoad();

	// The asset may be left over from a previous bake. Creating a new object with the same name
	// on top of it is not allowed — the engine trips a check. And recreating it is not needed
	// either: the room levels reference the mesh by name, so we reuse the existing one and
	// rebuild its contents. That way a re-export does not break references or breed redirectors.
	UStaticMesh* Mesh = FindObject<UStaticMesh>(Package, *Request.AssetName);

	if (Mesh)
	{
		Mesh->Modify();
		Mesh->SetNumSourceModels(0);
		Mesh->GetStaticMaterials().Empty();
	}
	else
	{
		Mesh = NewObject<UStaticMesh>(Package, FName(*Request.AssetName),
			RF_Public | RF_Standalone);
	}

	// InitResources() must not be called here: it initialises the render resources, while
	// RenderData only appears inside Build(). Calling it before the build kills the render thread.
	Mesh->SetLightingGuid();

	FStaticMeshSourceModel& SourceModel = Mesh->AddSourceModel();
	// We know the normals exactly — having the engine recompute them is pointless and harmful:
	// at the seam between faces it would smooth out the right angles.
	SourceModel.BuildSettings.bRecomputeNormals = false;
	SourceModel.BuildSettings.bRecomputeTangents = true;
	SourceModel.BuildSettings.bRemoveDegenerates = true;
	SourceModel.BuildSettings.bGenerateLightmapUVs = false;

	FMeshDescription* MeshDescription = Mesh->CreateMeshDescription(0);
	if (!MeshDescription)
	{
		OutResult.bFailed = true;
		return false;
	}

	FStaticMeshAttributes Attributes(*MeshDescription);
	Attributes.Register();

	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> TexCoords = Attributes.GetVertexInstanceUVs();

	// One polygon group per gathered surface. A single mesh with several material slots beats
	// several meshes: the room stays one actor, and the slots keep the back wall and the maze
	// mass visually apart without doubling the draw setup.
	TArray<FPolygonGroupID> PolygonGroups;
	PolygonGroups.Reserve(Surfaces.Num());

	for (const FMazeSurface& Surface : Surfaces)
	{
		const FPolygonGroupID Group = MeshDescription->CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = Surface.SlotName;
		PolygonGroups.Add(Group);
	}

	MeshDescription->ReserveNewVertices(Quads.Num() * 4);
	MeshDescription->ReserveNewVertexInstances(Quads.Num() * 4);
	MeshDescription->ReserveNewPolygons(Quads.Num());

	for (const FMazeQuad& Quad : Quads)
	{
		TArray<FVertexInstanceID, TInlineAllocator<4>> Instances;

		// Reverse traversal: the corners are laid out for Cross(V1-V0, V2-V0), while the engine
		// needs the opposite winding. In the straight order every face points inwards —
		// from the outside they are culled, and the volume falls apart into flat panels.
		for (int32 Index = 3; Index >= 0; --Index)
		{
			const FVertexID Vertex = MeshDescription->CreateVertex();
			Positions[Vertex] = FVector3f(Quad.Corners[Index] - Origin);

			const FVertexInstanceID Instance = MeshDescription->CreateVertexInstance(Vertex);
			Normals[Instance] = Quad.Normal;
			TexCoords.Set(Instance, 0, Quad.UVs[Index]);

			Instances.Add(Instance);
		}

		MeshDescription->CreatePolygon(
			PolygonGroups[FMath::Clamp(Quad.Surface, 0, PolygonGroups.Num() - 1)], Instances);
	}

	Mesh->CommitMeshDescription(0);

	// The slots go in the same order the groups were created — the mesh matches them by index,
	// so any reshuffle here would paint the back wall with the maze material and the other way round.
	for (const FMazeSurface& Surface : Surfaces)
	{
		UMaterialInterface* Material = Surface.Material
			? Surface.Material
			: UMaterial::GetDefaultMaterial(MD_Surface);

		Mesh->GetStaticMaterials().Add(
			FStaticMaterial(Material, Surface.SlotName, Surface.SlotName));
	}

	Mesh->Build(true);

	// Only the play band gets collision: the player cannot reach the decoration.
	Mesh->CreateBodySetup();
	if (UBodySetup* BodySetup = Mesh->GetBodySetup())
	{
		BodySetup->AggGeom.BoxElems.Reset();

		if (FMazeDepthProfile::BandHasCollision(Request.Band))
		{
			OutResult.CollisionBoxes =
				FMazeCollisionBuilder::Build(Grid, Room, Origin, BodySetup->AggGeom.BoxElems);
		}

		if (OutResult.CollisionBoxes > 0)
		{
			BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			BodySetup->InvalidatePhysicsData();
			BodySetup->CreatePhysicsMeshes();
		}
		else
		{
			// The decoration bands carry no collision at all. There is no point preparing
			// physics meshes for empty geometry — the engine explicitly advises against it.
			BodySetup->bNeverNeedsCookedCollisionData = true;
			BodySetup->InvalidatePhysicsData();
		}
	}

	Mesh->PostEditChange();

	// The mesh build is asynchronous. Handing out — let alone saving to disk — an object that is
	// still compiling is not allowed: an unfinished state would be serialised.
	FStaticMeshCompilingManager::Get().FinishCompilation({ Mesh });

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Mesh);

	OutResult.Mesh = Mesh;
	return true;
}
