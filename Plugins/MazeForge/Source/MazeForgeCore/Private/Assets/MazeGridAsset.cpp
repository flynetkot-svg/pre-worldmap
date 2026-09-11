#include "Assets/MazeGridAsset.h"

#include "Assets/MazeBuildSettings.h"
#include "Generators/MazeGenerator_Manual.h"
#include "MazeForgeCore.h"
#include "Slicers/MazeSlicer_UniformGrid.h"

#define LOCTEXT_NAMESPACE "MazeForge"

UMazeGridAsset::UMazeGridAsset()
{
	// The instances are deliberately not created here: an Instanced property whose class
	// the designer changes from a dropdown must not be a default subobject on the CDO.
	// The starting values are set by the factory when the asset is created.
}

void UMazeGridAsset::EnsureDefaults()
{
	if (!Generator)
	{
		Generator = NewObject<UMazeGenerator_Manual>(this, NAME_None, RF_Transactional);
	}

	if (!Slicer)
	{
		Slicer = NewObject<UMazeSlicer_UniformGrid>(this, NAME_None, RF_Transactional);
	}
}

namespace
{
	/**
	 *  A 64-bit mixer. FNV-1a over eight-byte words, which is enough here: the hash is only ever
	 *  compared against itself, never stored anywhere a collision could be attacked.
	 */
	FORCEINLINE uint64 Mix(uint64 Hash, uint64 Value)
	{
		Hash ^= Value;
		Hash *= 0x100000001B3ULL;
		return Hash ^ (Hash >> 29);
	}

	FORCEINLINE uint64 MixFloat(uint64 Hash, double Value)
	{
		// Through the bit pattern rather than a cast: a cell size of 100.5 and one of 100 must
		// not land on the same number.
		uint64 Bits = 0;
		const float AsFloat = static_cast<float>(Value);
		FMemory::Memcpy(&Bits, &AsFloat, sizeof(float));
		return Mix(Hash, Bits);
	}

	uint64 MixSoftPath(uint64 Hash, const FSoftObjectPath& Path)
	{
		return Mix(Hash, GetTypeHash(Path.ToString()));
	}

	/**
	 *  Through the string, never through GetTypeHash(FName).
	 *
	 *  GetTypeHash(FName) is the name's slot in the global name pool, assigned in order of first
	 *  creation during that run of the editor. It is documented as unstable and it is: the same
	 *  room would hash differently after a restart, every stored hash would miss, and the whole
	 *  maze would rebake on the first press of Bake in every session — which is precisely the
	 *  work these hashes exist to avoid. GetTypeHash(FString) is a table-driven CRC and does not
	 *  move.
	 */
	uint64 MixName(uint64 Hash, FName Name)
	{
		return Mix(Hash, GetTypeHash(Name.ToString()));
	}

	uint64 MixVisual(uint64 Hash, const FMazeCellVisual& Visual)
	{
		Hash = MixSoftPath(Hash, Visual.Mesh.ToSoftObjectPath());
		Hash = MixSoftPath(Hash, Visual.MaterialOverride.ToSoftObjectPath());
		Hash = Mix(Hash, static_cast<uint64>(Visual.Variants.Num()));

		// By index, not by any container order: the variant index is what a cell stores, so the
		// order of this list is itself part of what the mesh will look like.
		for (const FMazeSurfaceVariant& Variant : Visual.Variants)
		{
			Hash = MixName(Hash, Variant.Name);
			Hash = MixSoftPath(Hash, Variant.Material.ToSoftObjectPath());
		}

		return Hash;
	}
}

int64 UMazeGridAsset::ComputeRoomHash(const FMazeRoomDesc& Room) const
{
	uint64 Hash = 0xCBF29CE484222325ULL;

	// --- what the room is
	Hash = MixName(Hash, Room.RoomId);
	Hash = Mix(Hash, static_cast<uint64>(Room.MinXZ.X));
	Hash = Mix(Hash, static_cast<uint64>(Room.MinXZ.Y));
	Hash = Mix(Hash, static_cast<uint64>(Room.MaxXZ.X));
	Hash = Mix(Hash, static_cast<uint64>(Room.MaxXZ.Y));

	// --- the geometry every cell is placed by
	Hash = MixFloat(Hash, Grid.CellSize.X);
	Hash = MixFloat(Hash, Grid.CellSize.Y);
	Hash = MixFloat(Hash, Grid.CellSize.Z);
	Hash = MixFloat(Hash, Grid.WorldOrigin.X);
	Hash = MixFloat(Hash, Grid.WorldOrigin.Y);
	Hash = MixFloat(Hash, Grid.WorldOrigin.Z);
	Hash = Mix(Hash, static_cast<uint64>(Grid.SizeXZ.X));
	Hash = Mix(Hash, static_cast<uint64>(Grid.SizeXZ.Y));

	Hash = Mix(Hash, static_cast<uint64>(Grid.Depth.BackgroundCells));
	Hash = Mix(Hash, static_cast<uint64>(Grid.Depth.PlayCells));
	Hash = Mix(Hash, static_cast<uint64>(Grid.Depth.ForegroundCells));
	Hash = Mix(Hash, static_cast<uint64>(Grid.Depth.BackdropCells));
	Hash = MixFloat(Hash, Grid.Depth.CollisionPaddingUU);

	// The world border is solid mass the grid never stores as cells, so it has to be hashed
	// separately or a room on the edge would not notice the border being switched off.
	Hash = Mix(Hash, static_cast<uint64>(Grid.Borders.ThicknessCells));
	Hash = Mix(Hash, static_cast<uint64>(
		(Grid.Borders.bCloseLeft   ? 1 : 0) | (Grid.Borders.bCloseRight ? 2 : 0) |
		(Grid.Borders.bCloseBottom ? 4 : 0) | (Grid.Borders.bCloseTop   ? 8 : 0)));

	// --- how the mesh is assembled
	if (const UMazeBuildSettings* Settings = BuildSettings.LoadSynchronous())
	{
		Hash = Mix(Hash, static_cast<uint64>(
			(Settings->bSplitByDepthBand   ? 1 : 0) |
			(Settings->bCullEnclosedCells  ? 2 : 0) |
			(Settings->bCullFarBoundaryFaces ? 4 : 0) |
			(Settings->bMergeMaterials     ? 8 : 0)));

		// Sorted, because a TMap does not promise an iteration order and a hash that depends on
		// one would flip between editor runs and rebake the whole maze for nothing.
		TArray<EMazeCellType> Types;
		Settings->Palette.GetKeys(Types);
		Types.Sort([](EMazeCellType A, EMazeCellType B) { return static_cast<uint8>(A) < static_cast<uint8>(B); });

		for (EMazeCellType Type : Types)
		{
			Hash = Mix(Hash, static_cast<uint64>(static_cast<uint8>(Type)));
			Hash = MixVisual(Hash, Settings->Palette[Type]);
		}
	}
	else
	{
		// No build settings is itself a state worth distinguishing: assigning them later changes
		// every mesh, and the hash has to notice that.
		Hash = Mix(Hash, 0xD15AB1EDULL);
	}

	// --- the cells themselves, plus a one-cell skirt
	//
	// The skirt is not caution, it is correctness. Face culling asks whether the neighbour is
	// solid, and the neighbour of a cell on the room's edge lives in the room next door. Without
	// the skirt, a wall built right against the seam would leave the neighbouring room holding a
	// face that should no longer be there, and nothing would ever rebuild it.
	const int32 MinX = Room.MinXZ.X - 1;
	const int32 MaxX = Room.MaxXZ.X;      // exclusive bound, so this is already +1
	const int32 MinZ = Room.MinXZ.Y - 1;
	const int32 MaxZ = Room.MaxXZ.Y;
	const int32 DepthCells = Grid.DepthCells();

	for (int32 X = MinX; X <= MaxX; ++X)
	{
		for (int32 Z = MinZ; Z <= MaxZ; ++Z)
		{
			for (int32 Y = 0; Y < DepthCells; ++Y)
			{
				const FMazeCell* Cell = Grid.Cells.Find(FIntVector(X, Y, Z));
				if (!Cell)
				{
					continue;
				}

				// The coordinate goes in as well: without it, moving a cell one step to the side
				// would leave the multiset of cell types unchanged.
				Hash = Mix(Hash, (static_cast<uint64>(static_cast<uint32>(X)) << 32)
					^ (static_cast<uint64>(static_cast<uint32>(Z)) << 8)
					^ static_cast<uint64>(static_cast<uint32>(Y)));
				Hash = Mix(Hash, (static_cast<uint64>(static_cast<uint8>(Cell->Type)) << 8)
					| static_cast<uint64>(Cell->PaletteIndex));
			}
		}
	}

	return static_cast<int64>(Hash);
}

bool UMazeGridAsset::IsRoomBaked(const FMazeRoomDesc& Room) const
{
	const int64* Baked = BakedRoomHashes.Find(Room.RoomId);
	return Baked && *Baked == ComputeRoomHash(Room);
}

int32 UMazeGridAsset::CountBakedRooms() const
{
#if WITH_EDITOR
	const int32 SettingsRevision = UMazeBuildSettings::GetEditRevision();
#else
	const int32 SettingsRevision = 0;
#endif

	// Two keys, because there are two places the answer can change from: the grid, which bumps
	// GridRevision and clears the memo directly, and the build settings asset, which the grid has
	// no way of hearing from.
	if (CachedBakedRooms != INDEX_NONE && CachedSettingsRevision == SettingsRevision)
	{
		return CachedBakedRooms;
	}

	CachedSettingsRevision = SettingsRevision;

	int32 Fresh = 0;
	for (const FMazeRoomDesc& Room : Rooms)
	{
		Fresh += IsRoomBaked(Room) ? 1 : 0;
	}

	CachedBakedRooms = Fresh;
	return Fresh;
}

bool UMazeGridAsset::AreBakedMeshesCurrent() const
{
	return Rooms.Num() > 0 && CountBakedRooms() == Rooms.Num();
}

void UMazeGridAsset::PruneBakedRoomHashes()
{
	if (BakedRoomHashes.Num() == 0)
	{
		return;
	}

	TSet<FName> Alive;
	Alive.Reserve(Rooms.Num());
	for (const FMazeRoomDesc& Room : Rooms)
	{
		Alive.Add(Room.RoomId);
	}

	// A re-slice with a different room size renames every room. Left alone, the old entries would
	// sit in the asset for ever, growing with each experiment.
	for (auto It = BakedRoomHashes.CreateIterator(); It; ++It)
	{
		if (!Alive.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	InvalidateBakedRoomCache();
}

FString UMazeGridAsset::GetSafeMazeName() const
{
	FString Safe;
	Safe.Reserve(MazeName.Len());

	// Filtered rather than validated. The field is typed by hand and ends up in a package path,
	// where a space or a slash is not a cosmetic problem: it is a save that fails, or a folder
	// that quietly appears one level higher than intended.
	for (const TCHAR Ch : MazeName)
	{
		if (FChar::IsAlnum(Ch) || Ch == TEXT('_'))
		{
			Safe.AppendChar(Ch);
		}
	}

	return Safe;
}

void UMazeGridAsset::PostLoad()
{
	Super::PostLoad();

	// Grids saved before the cleanup can still hold Ladder, Spawn and Marker cells. They are
	// passable and produce no geometry, so they cost nothing at export — but they do draw in the
	// preview, and a cell type that no brush can paint and no code reads is only there to be
	// mistaken for something meaningful. They are erased on load, once, silently.
	TArray<FIntVector> Legacy;

	for (const TPair<FIntVector, FMazeCell>& Pair : Grid.Cells)
	{
		const EMazeCellType Type = Pair.Value.Type;
		if (Type == EMazeCellType::Ladder || Type == EMazeCellType::Spawn
			|| Type == EMazeCellType::Marker)
		{
			Legacy.Add(Pair.Key);
		}
	}

	if (Legacy.Num() == 0)
	{
		return;
	}

	for (const FIntVector& Cell : Legacy)
	{
		Grid.Cells.Remove(Cell);
	}

	InvalidateBakedRoomCache();

	// Deliberately no NotifyGridChanged: this runs during load, where marking the package dirty
	// and bumping the revision would invalidate baked meshes that are in fact still correct.
	// Nothing about the mass of the maze changed here.
	UE_LOG(LogMazeForge, Log,
		TEXT("%s: %d legacy marker cells removed (ladder/spawn/marker are the spawner's job now)."),
		*GetName(), Legacy.Num());
}

void UMazeGridAsset::NotifyGridChanged()
{
	// This is the single point every change to the grid passes through — which is why the
	// revision counter lives right here. Move a single cell, re-slice the rooms, change the
	// depth profile — the baked meshes are stale, and the export finds that out from the
	// per-room hashes.
	++GridRevision;
	InvalidateBakedRoomCache();

	OnGridChanged.Broadcast();
	MarkPackageDirty();
}

void UMazeGridAsset::RunGenerator()
{
	if (!Generator)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("%s: no generator set."), *GetName());
		return;
	}

#if WITH_EDITOR
	Modify();
#endif

	// A generator with clearing enabled will wipe out everything the designer drew by hand.
	// We take the snapshot before the run — afterwards it would be too late.
	if (Generator->bClearBeforeGenerate)
	{
		AutoSnapshot(TEXT("Run Generator"));
	}

	Generator->Execute(Grid);
	NotifyGridChanged();
}

void UMazeGridAsset::SliceIntoRooms()
{
	if (!Slicer)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("%s: no slicer set."), *GetName());
		return;
	}

#if WITH_EDITOR
	Modify();
#endif

	Slicer->Execute(Grid, Rooms);
	PruneBakedRoomHashes();
	NotifyGridChanged();
}

void UMazeGridAsset::ClearGrid()
{
#if WITH_EDITOR
	Modify();
#endif

	AutoSnapshot(TEXT("Clear Grid"));

	Grid.Reset();
	Rooms.Reset();
	BakedRoomHashes.Reset();
	NotifyGridChanged();
}

void UMazeGridAsset::SaveSnapshot()
{
	if (Grid.NumCells() == 0)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Snapshot: the grid is empty, nothing to save."));
		return;
	}

#if WITH_EDITOR
	Modify();
#endif

	Snapshot = Grid;
	SnapshotInfo = FString::Printf(TEXT("%d cells, %dx%d, %s"),
		Grid.NumCells(), Grid.SizeXZ.X, Grid.SizeXZ.Y,
		*FDateTime::Now().ToString(TEXT("%d.%m.%Y %H:%M")));

	MarkPackageDirty();

	UE_LOG(LogMazeForge, Log, TEXT("Snapshot saved: %s"), *SnapshotInfo);
}

void UMazeGridAsset::RestoreSnapshot()
{
	if (Snapshot.NumCells() == 0)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Snapshot: empty, nothing to restore."));
		return;
	}

#if WITH_EDITOR
	Modify();
#endif

	// We swap rather than overwrite: if the restore was done by mistake, a second press
	// puts things back as they were. A one-way button would erase work by itself —
	// exactly what the snapshot is there to protect against.
	const FMazeGrid Current = Grid;
	Grid = Snapshot;
	Snapshot = Current;

	SnapshotInfo = FString::Printf(TEXT("%d cells, %dx%d, swap %s"),
		Snapshot.NumCells(), Snapshot.SizeXZ.X, Snapshot.SizeXZ.Y,
		*FDateTime::Now().ToString(TEXT("%d.%m.%Y %H:%M")));

	// The rooms belonged to the previous grid and after the swap correspond to nothing.
	Rooms.Reset();

	NotifyGridChanged();

	UE_LOG(LogMazeForge, Log,
		TEXT("Snapshot restored: %d cells. Pressing again puts it back."),
		Grid.NumCells());
}

void UMazeGridAsset::AutoSnapshot(const TCHAR* Reason)
{
	// We only capture what would be a loss: there is no point saving an empty grid, and
	// overwriting the snapshot with a blank one is a sure way to lose work.
	if (Grid.NumCells() == 0)
	{
		return;
	}

	Snapshot = Grid;
	SnapshotInfo = FString::Printf(TEXT("%d cells, %dx%d, auto before \"%s\", %s"),
		Grid.NumCells(), Grid.SizeXZ.X, Grid.SizeXZ.Y, Reason,
		*FDateTime::Now().ToString(TEXT("%d.%m.%Y %H:%M")));

	UE_LOG(LogMazeForge, Log,
		TEXT("Snapshot taken automatically before \"%s\": %d cells. Restore Snapshot brings it back."),
		Reason, Grid.NumCells());
}

bool UMazeGridAsset::IsFlat() const
{
	const int32 PlaneY = Grid.Depth.PlaneCellY();

	for (const TPair<FIntVector, FMazeCell>& Pair : Grid.Cells)
	{
		if (Pair.Key.Y != PlaneY)
		{
			return false;
		}
	}

	return true;
}

void UMazeGridAsset::BuildDepthVolume()
{
#if WITH_EDITOR
	Modify();
#endif

	// First we collect the columns and only then write: changing Cells while iterating
	// over them at the same time is not allowed.
	TMap<FIntPoint, FMazeCell> Columns;
	Columns.Reserve(Grid.Cells.Num());

	for (const TPair<FIntVector, FMazeCell>& Pair : Grid.Cells)
	{
		// We do not stretch the markers: a ladder and a spawn point are gameplay data,
		// not geometry, and there is no point smearing them across the decor bands.
		if (Pair.Value.IsSolid())
		{
			Columns.FindOrAdd(FIntPoint(Pair.Key.X, Pair.Key.Z)) = Pair.Value;
		}
	}

	// A safeguard, not a ban. The depth multiplies everything: 95 thousand columns at 22
	// cells give 2.09 million cells. That is exactly what the editor once crashed on — but
	// it crashed on the export and on running out of RHI address space, not here; since
	// then the export unloads finished rooms in batches (RoomsPerFlush). So we count, warn
	// and build. We stop hard only where there would not be enough memory for the grid
	// itself: the depth profile is the designer's decision, not ours.
	constexpr int64 WarnVolumeCells = 1000000;
	constexpr int64 MaxVolumeCells  = 8000000;

	const int64 Projected = static_cast<int64>(Columns.Num()) * Grid.DepthCells();

	if (Projected > MaxVolumeCells)
	{
		UE_LOG(LogMazeForge, Error,
			TEXT("Volume: %d columns at depth %d would give %lld cells — that is above the ")
			TEXT("hard limit of %lld, the grid itself will not take that much. Reduce the area ")
			TEXT("or the depth profile (currently BG %d / Play %d / FG %d)."),
			Columns.Num(), Grid.DepthCells(), Projected, MaxVolumeCells,
			Grid.Depth.BackgroundCells, Grid.Depth.PlayCells, Grid.Depth.ForegroundCells);
		return;
	}

	if (Projected > WarnVolumeCells)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Volume: %d columns at depth %d would give %lld cells (BG %d / Play %d / FG %d). ")
			TEXT("That is a lot: editing the volume with the brush will be heavy and the export slow. ")
			TEXT("Before exporting, check that Rooms Per Flush > 0 is set in Build Settings, ")
			TEXT("otherwise the editor will run into the RHI address space. You can get the plane ")
			TEXT("back for editing with the Flatten To Plane button."),
			Columns.Num(), Grid.DepthCells(), Projected,
			Grid.Depth.BackgroundCells, Grid.Depth.PlayCells, Grid.Depth.ForegroundCells);
	}

	// One memory expansion instead of a dozen rehashes along the way: at two million cells
	// that is a noticeable difference in how long the button press takes.
	Grid.Cells.Reserve(static_cast<int32>(Projected));

	int32 Added = 0;
	for (const TPair<FIntPoint, FMazeCell>& Column : Columns)
	{
		Added += Grid.SetColumnXZ(Column.Key.X, Column.Key.Y, Column.Value);
	}

	NotifyGridChanged();

	UE_LOG(LogMazeForge, Log,
		TEXT("Volume: %d columns, %d cells added, %d in total (BG %d / Play %d / FG %d)."),
		Columns.Num(), Added, Grid.NumCells(),
		Grid.Depth.BackgroundCells, Grid.Depth.PlayCells, Grid.Depth.ForegroundCells);
}

void UMazeGridAsset::FlattenToPlane()
{
#if WITH_EDITOR
	Modify();
#endif

	const int32 PlaneY = Grid.Depth.PlaneCellY();

	TMap<FIntPoint, FMazeCell> Plane;
	Plane.Reserve(Grid.Cells.Num());

	// Cells that keep their own slice and must survive the flattening as they are.
	TArray<TPair<FIntVector, FMazeCell>> Kept;

	for (const TPair<FIntVector, FMazeCell>& Pair : Grid.Cells)
	{
		// The back wall is a separate layer and has its own slice. Collapsing it into the drawing
		// plane would move the wall from behind the maze into the middle of it — and Build Depth
		// Volume would then smear it across the bands as if it were maze mass.
		if (Pair.Value.IsBackWall())
		{
			Kept.Add(Pair);
			continue;
		}

		const FIntPoint Key(Pair.Key.X, Pair.Key.Z);

		// Solid beats a marker: if a column holds both a wall and a ladder, the wall is the
		// one that must stay in the plane — otherwise flattening puts a hole in the maze.
		FMazeCell* Existing = Plane.Find(Key);
		if (!Existing)
		{
			Plane.Add(Key, Pair.Value);
		}
		else if (!Existing->IsSolid() && Pair.Value.IsSolid())
		{
			*Existing = Pair.Value;
		}
	}

	const int32 Before = Grid.NumCells();

	Grid.Cells.Reset();
	for (const TPair<FIntPoint, FMazeCell>& Cell : Plane)
	{
		Grid.Set(FIntVector(Cell.Key.X, PlaneY, Cell.Key.Y), Cell.Value);
	}

	for (const TPair<FIntVector, FMazeCell>& Cell : Kept)
	{
		Grid.Set(Cell.Key, Cell.Value);
	}

	NotifyGridChanged();

	UE_LOG(LogMazeForge, Log, TEXT("Plane: was %d cells, now %d (slice Y %d)."),
		Before, Grid.NumCells(), PlaneY);
}

void UMazeGridAsset::FillBackWall(uint8 Variant)
{
#if WITH_EDITOR
	Modify();
#endif

	const int32 WallY = Grid.Depth.BackWallPlaneCellY();
	const int32 PlaneY = Grid.Depth.PlaneCellY();

	// Collect first, write after: adding to Cells while iterating them is not allowed.
	TArray<FIntVector> ToPaint;

	for (int32 X = 0; X < Grid.SizeXZ.X; ++X)
	{
		for (int32 Z = 0; Z < Grid.SizeXZ.Y; ++Z)
		{
			// Only behind empty columns: a wall behind solid mass is never seen and would be
			// pure geometry cost. The test is against the drawing plane, so this works on a
			// flat layout just as well as on a built volume.
			if (Grid.IsSolid(FIntVector(X, PlaneY, Z)))
			{
				continue;
			}

			ToPaint.Add(FIntVector(X, WallY, Z));
		}
	}

	int32 Added = 0;
	Grid.Cells.Reserve(Grid.Cells.Num() + ToPaint.Num());

	for (const FIntVector& Cell : ToPaint)
	{
		Added += Grid.Set(Cell, FMazeCell(EMazeCellType::BackWall, Variant)) ? 1 : 0;
	}

	NotifyGridChanged();

	UE_LOG(LogMazeForge, Log,
		TEXT("Back wall: %d cells painted in slice Y %d, variant %d. Cut the windows with the brush."),
		Added, WallY, Variant);
}

void UMazeGridAsset::ClearBackWall()
{
#if WITH_EDITOR
	Modify();
#endif

	TArray<FIntVector> ToRemove;

	for (const TPair<FIntVector, FMazeCell>& Pair : Grid.Cells)
	{
		if (Pair.Value.IsBackWall())
		{
			ToRemove.Add(Pair.Key);
		}
	}

	for (const FIntVector& Cell : ToRemove)
	{
		Grid.Clear(Cell);
	}

	NotifyGridChanged();

	UE_LOG(LogMazeForge, Log, TEXT("Back wall: %d cells removed."), ToRemove.Num());
}

const FMazeRoomDesc* UMazeGridAsset::FindRoom(FName RoomId) const
{
	return Rooms.FindByPredicate([RoomId](const FMazeRoomDesc& Room)
	{
		return Room.RoomId == RoomId;
	});
}

#if WITH_EDITOR
void UMazeGridAsset::PostEditUndo()
{
	Super::PostEditUndo();

	// The transaction has already put Grid and BakedRoomHashes back. Everything derived from them
	// has to be dropped by hand, and the preview has to be told, because no setter ran.
	InvalidateBakedRoomCache();
	OnGridChanged.Broadcast();
}

void UMazeGridAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// The bounds, the scale and the depth profile all change the preview geometry.
	NotifyGridChanged();
}
#endif

#undef LOCTEXT_NAMESPACE
