#include "Export/MazeMeshBuilder_Faces.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Assets/MazeBuildSettings.h"
#include "Data/MazeGrid.h"
#include "Data/MazeRoomDesc.h"
#include "Engine/StaticMesh.h"
#include "Export/MazeCollisionBuilder.h"
#include "Export/MazeExportUtils.h"
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

namespace
{
	// ------------------------------------------------------------------ rough faces

	const FIntVector RoughFaceOffsets[6] = {
		FIntVector( 1,  0,  0), FIntVector(-1,  0,  0),
		FIntVector( 0,  1,  0), FIntVector( 0, -1,  0),
		FIntVector( 0,  0,  1), FIntVector( 0,  0, -1)
	};

	/** The planar unwrap of MakeQuad for any point of a face: one cell is one tile. */
	FVector2f RoughFaceUV(int32 FaceIndex, const FVector& C)
	{
		const float U = (FaceIndex < 2) ? static_cast<float>(C.Y) : static_cast<float>(C.X);
		const float V = (FaceIndex < 4) ? static_cast<float>(C.Z) : static_cast<float>(C.Y);
		return FVector2f(U / 100.f, -V / 100.f);
	}

	/** A stable pseudo-random number in [0, 1] for a lattice point, a seed and a salt. */
	double RoughUnit(int32 X, int32 Y, int32 Z, uint32 Seed, uint32 Salt)
	{
		uint64 H = 0x9E3779B97F4A7C15ULL ^ (static_cast<uint64>(Seed) << 32) ^ Salt;
		H ^= static_cast<uint64>(static_cast<uint32>(X)) * 0xBF58476D1CE4E5B9ULL;
		H = (H ^ (H >> 31)) * 0x94D049BB133111EBULL;
		H ^= static_cast<uint64>(static_cast<uint32>(Y)) * 0xBF58476D1CE4E5B9ULL;
		H = (H ^ (H >> 29)) * 0x94D049BB133111EBULL;
		H ^= static_cast<uint64>(static_cast<uint32>(Z)) * 0xBF58476D1CE4E5B9ULL;
		H = (H ^ (H >> 32)) * 0x94D049BB133111EBULL;
		return static_cast<double>(static_cast<uint32>(H ^ (H >> 29))) / 4294967295.0;
	}

	/** One triangle of a chipped face. Corners wound like FMazeQuad: (V1-V0) x (V2-V0) outwards. */
	struct FMazeTri
	{
		FVector Corners[3];
		FVector2f UVs[3];
		FVector3f Normal;
		int32 Surface = 0;
	};

	/**
	 *  The Rough brush at bake: the faces it was painted over, cut fine and pushed about in flat
	 *  chips with sharp edges, as a pick leaves rock.
	 *
	 *  Every displacement is a function of the world position alone — the direction, the chip,
	 *  how much of it applies — so a vertex shared by two faces, two bands or two rooms moves the
	 *  same way from every side, and nothing opens up along a seam. Where the rough faces meet
	 *  plain ones the displacement falls to nothing, so the plain quads, merged into long
	 *  rectangles, still meet them edge to edge.
	 */
	class FMazeRough
	{
	public:
		FMazeRough(const FMazeGrid& InGrid, const UMazeBuildSettings* Settings)
			: Grid(InGrid)
		{
			if (Settings)
			{
				Amp = FMath::Max(0.0, static_cast<double>(Settings->RoughAmplitudeCm));
				Chip = FMath::Max(5.0, static_cast<double>(Settings->RoughChipSizeCm));
				Step = FMath::Max(2.0, static_cast<double>(Settings->RoughSubdivisionCm));
				Overhang = FMath::Clamp(static_cast<double>(Settings->RoughOverhang), 0.0, 1.0);
				Tilt = FMath::Max(0.0, static_cast<double>(Settings->RoughTilt));
				Seed = static_cast<uint32>(Settings->RoughSeed);
				SmoothRadius = FMath::Max(0.0, static_cast<double>(Settings->RoughSmoothRadiusCells));
				SmoothMax = FMath::Max(0.0, static_cast<double>(Settings->RoughSmoothMaxCm));
				SmoothFill = FMath::Clamp(static_cast<double>(Settings->RoughSmoothFill), 0.0, 1.0);
				bSmoothXZOnly = Settings->bRoughSmoothXZOnly;
			}
			LatticeOrigin = Grid.GetCellBounds(FIntVector::ZeroValue).Min;
		}

		/** Painted over, by the cell itself or by the air in front of it — and not the walkway. */
		bool IsRoughFace(const FIntVector& Cell, int32 Face) const
		{
			const FIntVector Neighbour = Cell + RoughFaceOffsets[Face];
			if (!Grid.RoughCells.Contains(Cell) && !Grid.RoughCells.Contains(Neighbour))
			{
				return false;
			}

			if (const FMazeCell* Painted = Grid.Cells.Find(Cell))
			{
				// The back wall is a painted surface behind the maze, brick as often as not.
				if (Painted->IsBackWall())
				{
					return false;
				}
				// The floor the player runs along stays flat; its edges and the walls do not.
				if (Face == 4 && Painted->Type == EMazeCellType::Floor)
				{
					return false;
				}
			}
			return true;
		}

		/** One cell face, cut into a fine mesh and chipped. */
		void EmitFace(const FMazeQuad& Base, int32 Face, int32 Surface, TArray<FMazeTri>& Out)
		{
			const FVector Q0 = Base.Corners[0];
			const FVector EU = Base.Corners[1] - Q0;
			const FVector EV = Base.Corners[3] - Q0;
			const int32 NU = FMath::Clamp(FMath::RoundToInt(EU.Size() / Step), 1, 64);
			const int32 NV = FMath::Clamp(FMath::RoundToInt(EV.Size() / Step), 1, 64);

			const float W0 = CornerWeight(LatticeOf(Base.Corners[0]));
			const float W1 = CornerWeight(LatticeOf(Base.Corners[1]));
			const float W2 = CornerWeight(LatticeOf(Base.Corners[2]));
			const float W3 = CornerWeight(LatticeOf(Base.Corners[3]));

			TArray<FVector> Points;
			TArray<FVector2f> UVs;
			Points.SetNumUninitialized((NU + 1) * (NV + 1));
			UVs.SetNumUninitialized((NU + 1) * (NV + 1));

			for (int32 J = 0; J <= NV; ++J)
			{
				for (int32 I = 0; I <= NU; ++I)
				{
					const double U = static_cast<double>(I) / NU;
					const double V = static_cast<double>(J) / NV;
					const FVector P = Q0 + EU * U + EV * V;

					// Bilinear over the face: along an edge it depends on that edge's two corners
					// only, so the face across the edge computes the very same number.
					const float W = static_cast<float>((1 - U) * (1 - V) * W0 + U * (1 - V) * W1
						+ U * V * W2 + (1 - U) * V * W3);

					Points[J * (NU + 1) + I] = Displace(P, W);
					UVs[J * (NU + 1) + I] = RoughFaceUV(Face, P); // replaced per facet below
				}
			}

			auto AddTri = [&](int32 A, int32 B, int32 C)
			{
				FMazeTri Tri;
				Tri.Corners[0] = Points[A];
				Tri.Corners[1] = Points[B];
				Tri.Corners[2] = Points[C];
				Tri.UVs[0] = UVs[A];
				Tri.UVs[1] = UVs[B];
				Tri.UVs[2] = UVs[C];
				Tri.Surface = Surface;

				// Flat shading on purpose: every facet catches the light on its own, which is
				// exactly what reads as hewn.
				const FVector N = FVector::CrossProduct(Tri.Corners[1] - Tri.Corners[0], Tri.Corners[2] - Tri.Corners[0]);
				Tri.Normal = N.SizeSquared() > UE_KINDA_SMALL_NUMBER ? FVector3f(N.GetSafeNormal()) : Base.Normal;

				// The texture is projected along the axis the facet itself faces, from where its
				// corners actually are. Projected from the flat face it came from, the steep side of
				// a chip got the texture pulled into streaks across it.
				const FVector3f Facing = Tri.Normal.GetAbs();
				const int32 Axis = (Facing.X >= Facing.Y && Facing.X >= Facing.Z) ? 0 : (Facing.Y >= Facing.Z ? 2 : 4);
				for (int32 K = 0; K < 3; ++K)
				{
					Tri.UVs[K] = RoughFaceUV(Axis, Tri.Corners[K]);
				}
				Out.Add(Tri);
			};

			for (int32 J = 0; J < NV; ++J)
			{
				for (int32 I = 0; I < NU; ++I)
				{
					const int32 A = J * (NU + 1) + I;
					const int32 B = A + 1;
					const int32 C = A + NU + 2;
					const int32 D = A + NU + 1;

					// Alternating diagonals, or every quad tears the same way and the chips line up.
					if (((I + J) & 1) == 0)
					{
						AddTri(A, B, C);
						AddTri(A, C, D);
					}
					else
					{
						AddTri(A, B, D);
						AddTri(B, C, D);
					}
				}
			}
		}

	private:
		const FMazeGrid& Grid;
		double Amp = 20.0;
		double Chip = 40.0;
		double Step = 12.5;
		double Overhang = 0.25;
		double Tilt = 0.7;
		uint32 Seed = 1;
		double SmoothRadius = 1.5;
		double SmoothMax = 40.0;
		double SmoothFill = 0.2;
		bool bSmoothXZOnly = false;
		FVector LatticeOrigin = FVector::ZeroVector;
		TMap<FIntVector, float> Weights;

		/** The smoothed solidity, sampled on a half-cell lattice and kept: many points share each node. */
		TMap<FIntVector, float> SmoothNodes;

		bool IsSmoothing() const
		{
			return SmoothRadius >= 0.5 && SmoothMax > 0.0;
		}

		/**
		 *  Solidity blurred over SmoothRadius cells, at one node of the half-cell lattice. A tent
		 *  over the cells round it; in the side-view-only mode the depth is only interpolated
		 *  between the two nearest slices, never blurred, so the edge into the depth stays sharp.
		 */
		float SmoothNode(const FIntVector& N)
		{
			if (const float* Known = SmoothNodes.Find(N))
			{
				return *Known;
			}

			// The node in cell-centre coordinates: cell I has its centre at I.
			const double CX = N.X * 0.5 - 0.5;
			const double CY = N.Y * 0.5 - 0.5;
			const double CZ = N.Z * 0.5 - 0.5;
			const double R = SmoothRadius;

			const int32 YFrom = bSmoothXZOnly ? FMath::FloorToInt(CY) : FMath::FloorToInt(CY - R);
			const int32 YTo = bSmoothXZOnly ? FMath::FloorToInt(CY) + 1 : FMath::CeilToInt(CY + R);

			double Sum = 0.0;
			double Total = 0.0;
			for (int32 Z = FMath::FloorToInt(CZ - R); Z <= FMath::CeilToInt(CZ + R); ++Z)
			{
				for (int32 Y = YFrom; Y <= YTo; ++Y)
				{
					for (int32 X = FMath::FloorToInt(CX - R); X <= FMath::CeilToInt(CX + R); ++X)
					{
						const double DX = X - CX;
						const double DY = Y - CY;
						const double DZ = Z - CZ;
						const double W = bSmoothXZOnly
							? FMath::Max(0.0, 1.0 - FMath::Sqrt(DX * DX + DZ * DZ) / R) * FMath::Max(0.0, 1.0 - FMath::Abs(DY))
							: FMath::Max(0.0, 1.0 - FMath::Sqrt(DX * DX + DY * DY + DZ * DZ) / R);
						if (W <= 0.0)
						{
							continue;
						}
						Total += W;
						Sum += Geometry(FIntVector(X, Y, Z)) ? W : 0.0;
					}
				}
			}

			const float Value = Total > 0.0 ? static_cast<float>(Sum / Total) : 0.0f;
			SmoothNodes.Add(N, Value);
			return Value;
		}

		/** The smoothed solidity anywhere: trilinear between the eight nodes round P. */
		double Smoothed(const FVector& P)
		{
			const FVector Half = Grid.CellSize * 0.5;
			const double X = (P.X - LatticeOrigin.X) / Half.X;
			const double Y = (P.Y - LatticeOrigin.Y) / Half.Y;
			const double Z = (P.Z - LatticeOrigin.Z) / Half.Z;
			const int32 X0 = FMath::FloorToInt(X);
			const int32 Y0 = FMath::FloorToInt(Y);
			const int32 Z0 = FMath::FloorToInt(Z);
			const double FX = X - X0;
			const double FY = Y - Y0;
			const double FZ = Z - Z0;

			double Sum = 0.0;
			for (int32 K = 0; K < 8; ++K)
			{
				const int32 Ix = K & 1;
				const int32 Iy = (K >> 1) & 1;
				const int32 Iz = (K >> 2) & 1;
				Sum += SmoothNode(FIntVector(X0 + Ix, Y0 + Iy, Z0 + Iz))
					* (Ix ? FX : 1 - FX) * (Iy ? FY : 1 - FY) * (Iz ? FZ : 1 - FZ);
			}
			return Sum;
		}

		/** Into the rock by the smoothed solidity: on a staircase that is across the slope, not into one step. */
		FVector SmoothInward(const FVector& P)
		{
			const FVector H = Grid.CellSize * 0.25;
			const FVector G(
				Smoothed(P + FVector(H.X, 0, 0)) - Smoothed(P - FVector(H.X, 0, 0)),
				bSmoothXZOnly ? 0.0 : Smoothed(P + FVector(0, H.Y, 0)) - Smoothed(P - FVector(0, H.Y, 0)),
				Smoothed(P + FVector(0, 0, H.Z)) - Smoothed(P - FVector(0, 0, H.Z)));
			return G.SizeSquared() > 1e-10 ? G.GetSafeNormal() : FVector::ZeroVector;
		}

		/**
		 *  P moved onto the smoothed surface — where the blurred solidity is one half — along the
		 *  way into the rock. An outer corner is cut in, up to SmoothMax; an inner corner filled,
		 *  up to SmoothMax x SmoothFill, since what fills it stands in the passage.
		 */
		FVector Smooth(const FVector& P, const FVector& Dir)
		{
			auto Level = [this, &P, &Dir](double T) { return Smoothed(P + Dir * T) - 0.5; };

			const double At = Level(0.0);
			double Lo = 0.0;
			double Hi = 0.0;
			if (At < 0.0)
			{
				// Too much air round it: an outer corner, cut into the rock.
				Hi = SmoothMax;
				if (Level(Hi) < 0.0)
				{
					return P + Dir * Hi;
				}
			}
			else if (At > 0.0)
			{
				// Too much rock: an inner corner, filled a little.
				Lo = -SmoothMax * SmoothFill;
				if (Lo >= 0.0 || Level(Lo) > 0.0)
				{
					return P + Dir * Lo;
				}
			}
			else
			{
				return P;
			}

			for (int32 Iteration = 0; Iteration < 10; ++Iteration)
			{
				const double Mid = 0.5 * (Lo + Hi);
				if (Level(Mid) < 0.0)
				{
					Lo = Mid;
				}
				else
				{
					Hi = Mid;
				}
			}
			return P + Dir * (0.5 * (Lo + Hi));
		}

		bool Geometry(const FIntVector& C) const
		{
			return Grid.IsSolidForGeometry(C);
		}

		/** Exactly the builder's rule: solid here, and not solid inside the grid next door. */
		bool HasFace(const FIntVector& Cell, const FIntVector& Neighbour) const
		{
			return Geometry(Cell) && !(Grid.IsInside(Neighbour) && Geometry(Neighbour));
		}

		FIntVector LatticeOf(const FVector& P) const
		{
			return FIntVector(
				FMath::RoundToInt((P.X - LatticeOrigin.X) / Grid.CellSize.X),
				FMath::RoundToInt((P.Y - LatticeOrigin.Y) / Grid.CellSize.Y),
				FMath::RoundToInt((P.Z - LatticeOrigin.Z) / Grid.CellSize.Z));
		}

		/**
		 *  1 where every visible face meeting this lattice corner is rough, 0 where any is plain.
		 *  The plain faces are merged into long rectangles with their corners only, so anything
		 *  they touch has to stay where it was.
		 */
		float CornerWeight(const FIntVector& L)
		{
			if (const float* Known = Weights.Find(L))
			{
				return *Known;
			}

			float Result = 1.0f;
			for (int32 Dz = -1; Dz <= 0 && Result > 0.0f; ++Dz)
			{
				for (int32 Dy = -1; Dy <= 0 && Result > 0.0f; ++Dy)
				{
					for (int32 Dx = -1; Dx <= 0 && Result > 0.0f; ++Dx)
					{
						const FIntVector Cell = L + FIntVector(Dx, Dy, Dz);
						if (!Geometry(Cell))
						{
							continue;
						}

						// The three faces of this cell that touch the corner.
						const FIntVector Across[3] = {
							FIntVector(Dx == -1 ? L.X : L.X - 1, Cell.Y, Cell.Z),
							FIntVector(Cell.X, Dy == -1 ? L.Y : L.Y - 1, Cell.Z),
							FIntVector(Cell.X, Cell.Y, Dz == -1 ? L.Z : L.Z - 1)
						};
						const int32 Faces[3] = { Dx == -1 ? 0 : 1, Dy == -1 ? 2 : 3, Dz == -1 ? 4 : 5 };

						for (int32 K = 0; K < 3; ++K)
						{
							if (HasFace(Cell, Across[K]) && !IsRoughFace(Cell, Faces[K]))
							{
								Result = 0.0f;
								break;
							}
						}
					}
				}
			}

			Weights.Add(L, Result);
			return Result;
		}

		/** How solid the world is around P, smoothly: mass 1, air 0, between cell centres blended. */
		double Occupancy(const FVector& P) const
		{
			const double X = (P.X - LatticeOrigin.X) / Grid.CellSize.X - 0.5;
			const double Y = (P.Y - LatticeOrigin.Y) / Grid.CellSize.Y - 0.5;
			const double Z = (P.Z - LatticeOrigin.Z) / Grid.CellSize.Z - 0.5;
			const int32 X0 = FMath::FloorToInt(X);
			const int32 Y0 = FMath::FloorToInt(Y);
			const int32 Z0 = FMath::FloorToInt(Z);
			const double FX = X - X0;
			const double FY = Y - Y0;
			const double FZ = Z - Z0;

			double Sum = 0.0;
			for (int32 K = 0; K < 8; ++K)
			{
				const int32 Ix = K & 1;
				const int32 Iy = (K >> 1) & 1;
				const int32 Iz = (K >> 2) & 1;
				if (Geometry(FIntVector(X0 + Ix, Y0 + Iy, Z0 + Iz)))
				{
					Sum += (Ix ? FX : 1 - FX) * (Iy ? FY : 1 - FY) * (Iz ? FZ : 1 - FZ);
				}
			}
			return Sum;
		}

		/** Into the rock: up the slope of Occupancy. On a flat face that is straight in; at a corner, across it. */
		FVector Inward(const FVector& P) const
		{
			const FVector H = Grid.CellSize * 0.25;
			const FVector G(
				Occupancy(P + FVector(H.X, 0, 0)) - Occupancy(P - FVector(H.X, 0, 0)),
				Occupancy(P + FVector(0, H.Y, 0)) - Occupancy(P - FVector(0, H.Y, 0)),
				Occupancy(P + FVector(0, 0, H.Z)) - Occupancy(P - FVector(0, 0, H.Z)));
			return G.SizeSquared() > 1e-8 ? G.GetSafeNormal() : FVector::ZeroVector;
		}

		/**
		 *  The chip P falls in, 0..1: the nearest of a jittered lattice of sites, each with a
		 *  depth and a tilted plane of its own. Constant-ish inside a chip, a sharp step between
		 *  two — flat facets with hard edges, not rolling noise.
		 */
		double Chip01(const FVector& P) const
		{
			const FIntVector G(FMath::FloorToInt(P.X / Chip), FMath::FloorToInt(P.Y / Chip), FMath::FloorToInt(P.Z / Chip));

			double Best = TNumericLimits<double>::Max();
			FIntVector BestCell = G;
			FVector BestSite = FVector::ZeroVector;

			for (int32 Dz = -1; Dz <= 1; ++Dz)
			{
				for (int32 Dy = -1; Dy <= 1; ++Dy)
				{
					for (int32 Dx = -1; Dx <= 1; ++Dx)
					{
						const FIntVector C = G + FIntVector(Dx, Dy, Dz);
						const FVector Site(
							(C.X + 0.15 + 0.7 * RoughUnit(C.X, C.Y, C.Z, Seed, 1)) * Chip,
							(C.Y + 0.15 + 0.7 * RoughUnit(C.X, C.Y, C.Z, Seed, 2)) * Chip,
							(C.Z + 0.15 + 0.7 * RoughUnit(C.X, C.Y, C.Z, Seed, 3)) * Chip);
						const double D = FVector::DistSquared(P, Site);
						if (D < Best)
						{
							Best = D;
							BestCell = C;
							BestSite = Site;
						}
					}
				}
			}

			const double Depth = RoughUnit(BestCell.X, BestCell.Y, BestCell.Z, Seed, 4);
			const FVector Slope(
				(RoughUnit(BestCell.X, BestCell.Y, BestCell.Z, Seed, 5) * 2.0 - 1.0) * Tilt,
				(RoughUnit(BestCell.X, BestCell.Y, BestCell.Z, Seed, 6) * 2.0 - 1.0) * Tilt,
				(RoughUnit(BestCell.X, BestCell.Y, BestCell.Z, Seed, 7) * 2.0 - 1.0) * Tilt);
			return FMath::Clamp(Depth + FVector::DotProduct(P - BestSite, Slope) / Chip, 0.0, 1.0);
		}

		FVector Displace(const FVector& P, float Weight)
		{
			if (Weight <= 0.0f || (Amp <= 0.0 && !IsSmoothing()))
			{
				return P;
			}

			// The cube outline smoothed first, then chipped: a staircase becomes a hewn slope
			// rather than hewn steps. Faded by the same weight, so it meets the plain faces.
			FVector Base = P;
			FVector Dir = Inward(P);
			if (IsSmoothing())
			{
				const FVector Across = SmoothInward(P);
				if (!Across.IsZero())
				{
					Dir = Across;
					Base = FMath::Lerp(P, Smooth(P, Dir), static_cast<double>(Weight));
				}
			}

			if (Dir.IsZero() || Amp <= 0.0)
			{
				return Base;
			}

			// Mostly into the rock; Overhang of it may stand out of it.
			const double Depth = FMath::Lerp(-Overhang, 1.0, Chip01(Base));
			return Base + Dir * (Amp * Depth * Weight);
		}
	};
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

	// With merging on, a visible face is not turned into a quad on the spot: it is filed under
	// its direction, its layer along that direction and its surface, by its two in-plane
	// coordinates, and the faces of each file are joined into rectangles afterwards.
	const bool bMerge = !Request.Settings || Request.Settings->bMergeCoplanarFaces;
	TMap<TTuple<int32, int32, int32>, TArray<FIntPoint>> FacePlanes; // (face, layer, surface) -> (u, v)

	// Faces painted with the Rough brush are not merged: they come out as chipped triangles.
	TArray<FMazeTri> Tris;
	FMazeRough Rough(Grid, Request.Settings);
	const bool bAnyRough = Grid.RoughCells.Num() > 0;

	// Face 0/1 lie across X, 2/3 across Y, 4/5 across Z; the other two axes are (u, v).
	auto ToPlane = [](int32 Face, const FIntVector& Cell, int32& OutLayer) -> FIntPoint
	{
		if (Face < 2) { OutLayer = Cell.X; return FIntPoint(Cell.Y, Cell.Z); }
		if (Face < 4) { OutLayer = Cell.Y; return FIntPoint(Cell.X, Cell.Z); }
		OutLayer = Cell.Z;
		return FIntPoint(Cell.X, Cell.Y);
	};
	auto FromPlane = [](int32 Face, int32 Layer, int32 U, int32 V) -> FIntVector
	{
		if (Face < 2) { return FIntVector(Layer, U, V); }
		if (Face < 4) { return FIntVector(U, Layer, V); }
		return FIntVector(U, V, Layer);
	};

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

					if (bAnyRough && Rough.IsRoughFace(Cell, Face))
					{
						FMazeQuad Base;
						MakeQuad(CellBounds, Face, Base);
						Base.Surface = Surface;
						Rough.EmitFace(Base, Face, Surface, Tris);
						continue;
					}

					if (bMerge)
					{
						int32 Layer = 0;
						const FIntPoint UV = ToPlane(Face, Cell, Layer);
						FacePlanes.FindOrAdd(MakeTuple(Face, Layer, Surface)).Add(UV);
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

	// Greedy rectangles per plane: widest run along u first, then as many rows along v as
	// carry the same run whole. Every corner stays on the cell lattice, and the quad is made from
	// the bounds of the cells it covers, so position, normal and UVs come out as the cells' did.
	for (const TPair<TTuple<int32, int32, int32>, TArray<FIntPoint>>& Plane : FacePlanes)
	{
		const int32 Face = Plane.Key.Get<0>();
		const int32 Layer = Plane.Key.Get<1>();
		const int32 Surface = Plane.Key.Get<2>();

		FIntPoint Min(MAX_int32, MAX_int32);
		FIntPoint Max(MIN_int32, MIN_int32);
		for (const FIntPoint& P : Plane.Value)
		{
			Min = FIntPoint(FMath::Min(Min.X, P.X), FMath::Min(Min.Y, P.Y));
			Max = FIntPoint(FMath::Max(Max.X, P.X), FMath::Max(Max.Y, P.Y));
		}

		const int32 W = Max.X - Min.X + 1;
		const int32 H = Max.Y - Min.Y + 1;
		TBitArray<> Bits(false, W * H);
		for (const FIntPoint& P : Plane.Value)
		{
			Bits[(P.Y - Min.Y) * W + (P.X - Min.X)] = true;
		}

		for (int32 V = 0; V < H; ++V)
		{
			for (int32 U = 0; U < W; ++U)
			{
				if (!Bits[V * W + U])
				{
					continue;
				}

				int32 RunU = 1;
				while (U + RunU < W && Bits[V * W + U + RunU])
				{
					++RunU;
				}

				int32 RunV = 1;
				for (; V + RunV < H; ++RunV)
				{
					bool bFull = true;
					for (int32 i = 0; i < RunU && bFull; ++i)
					{
						bFull = Bits[(V + RunV) * W + U + i];
					}
					if (!bFull)
					{
						break;
					}
				}

				for (int32 Dv = 0; Dv < RunV; ++Dv)
				{
					for (int32 Du = 0; Du < RunU; ++Du)
					{
						Bits[(V + Dv) * W + U + Du] = false;
					}
				}

				const FBox Bounds =
					Grid.GetCellBounds(FromPlane(Face, Layer, Min.X + U, Min.Y + V))
					+ Grid.GetCellBounds(FromPlane(Face, Layer, Min.X + U + RunU - 1, Min.Y + V + RunV - 1));

				FMazeQuad Quad;
				MakeQuad(Bounds, Face, Quad);
				Quad.Surface = Surface;
				Quads.Add(Quad);
			}
		}
	}

	if (Quads.Num() == 0 && Tris.Num() == 0)
	{
		// The one honest "nothing here": a band with no visible surface. bFailed stays clear.
		return false;
	}

	OutResult.Quads = Quads.Num() + Tris.Num();

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

	if (!MazeExport::ClearNameFor(Package, Request.AssetName, UStaticMesh::StaticClass()))
	{
		OutResult.bFailed = true;
		return false;
	}

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

	MeshDescription->ReserveNewVertices(Quads.Num() * 4 + Tris.Num() * 3);
	MeshDescription->ReserveNewVertexInstances(Quads.Num() * 4 + Tris.Num() * 3);
	MeshDescription->ReserveNewPolygons(Quads.Num() + Tris.Num());

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

	// The chipped faces, the same way round as the quads.
	for (const FMazeTri& Tri : Tris)
	{
		TArray<FVertexInstanceID, TInlineAllocator<3>> Instances;
		for (int32 Index = 2; Index >= 0; --Index)
		{
			const FVertexID Vertex = MeshDescription->CreateVertex();
			Positions[Vertex] = FVector3f(Tri.Corners[Index] - Origin);

			const FVertexInstanceID Instance = MeshDescription->CreateVertexInstance(Vertex);
			Normals[Instance] = Tri.Normal;
			TexCoords.Set(Instance, 0, Tri.UVs[Index]);
			Instances.Add(Instance);
		}

		MeshDescription->CreatePolygon(
			PolygonGroups[FMath::Clamp(Tri.Surface, 0, PolygonGroups.Num() - 1)], Instances);
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
