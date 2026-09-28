#include "Data/MazeGrid.h"

#include "MazeForgeCore.h"
#include "Misc/Compression.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

//  The on-disk form of FMazeGrid::Cells. Nothing outside this file knows the layout.
//
//      header   uint32 Magic, uint8 Version, uint8 Layout, uint8 bCompressed,
//               int32 NumCells, FIntVector Min, FIntVector Extent, int32 RawSize
//      payload  RawSize bytes, Oodle-compressed when bCompressed
//
//  Dense   two planes over the bounding box of the occupied cells, Type then PaletteIndex,
//          X fastest. An empty position is Type 0. A maze is long runs of the same byte,
//          which is exactly what the compressor eats.
//  Sparse  for a box that is mostly air (one stray cell far away): the cells as a list,
//          field by field, X[] Y[] Z[] Type[] Palette[], sorted Z, Y, X.
//
//  The smaller raw form wins.

namespace
{
	constexpr uint32 PackMagic   = 0x5043464D; // reads "MFCP" in a hex viewer
	constexpr uint8  PackVersion = 1;

	constexpr uint8 LayoutDense  = 0;
	constexpr uint8 LayoutSparse = 1;

	constexpr int64 SparseBytesPerCell = 3 * sizeof(int32) + 2;

	// One block has to fit an int32 with room to spare; beyond that the grid stays tagged.
	constexpr int64 MaxRawSize = MAX_int32 / 2;

	struct FPackHeader
	{
		uint32 Magic = PackMagic;
		uint8 Version = PackVersion;
		uint8 Layout = LayoutDense;
		uint8 bCompressed = 0;
		int32 NumCells = 0;
		FIntVector Min = FIntVector::ZeroValue;
		FIntVector Extent = FIntVector::ZeroValue;
		int32 RawSize = 0;

		friend FArchive& operator<<(FArchive& Ar, FPackHeader& H)
		{
			Ar << H.Magic << H.Version << H.Layout << H.bCompressed << H.NumCells
				<< H.Min << H.Extent << H.RawSize;
			return Ar;
		}
	};

	int64 DenseIndex(const FIntVector& Local, const FIntVector& Extent)
	{
		return (int64(Local.Z) * Extent.Y + Local.Y) * Extent.X + Local.X;
	}

	void WriteInt32(uint8* Plane, int32 Index, int32 Value)
	{
		FMemory::Memcpy(Plane + int64(Index) * sizeof(int32), &Value, sizeof(int32));
	}

	int32 ReadInt32(const uint8* Plane, int32 Index)
	{
		int32 Value;
		FMemory::Memcpy(&Value, Plane + int64(Index) * sizeof(int32), sizeof(int32));
		return Value;
	}
}

bool FMazeGrid::BeginPack(TMap<FIntVector, FMazeCell>& OutStash, bool bCompress)
{
	// Outside a save the block is always empty. If it is not, it came from disk and could not
	// be read: writing it back untouched is the only way not to lose what is in it.
	if (PackedCells.Num() > 0)
	{
		return false;
	}

	FPackHeader Header;
	Header.NumCells = Cells.Num();

	if (Header.NumCells > 0)
	{
		FIntVector Min(MAX_int32), Max(MIN_int32);
		for (const TPair<FIntVector, FMazeCell>& Pair : Cells)
		{
			const FIntVector& P = Pair.Key;
			Min = FIntVector(FMath::Min(Min.X, P.X), FMath::Min(Min.Y, P.Y), FMath::Min(Min.Z, P.Z));
			Max = FIntVector(FMath::Max(Max.X, P.X), FMath::Max(Max.Y, P.Y), FMath::Max(Max.Z, P.Z));
		}

		const int64 Ex = int64(Max.X) - Min.X + 1;
		const int64 Ey = int64(Max.Y) - Min.Y + 1;
		const int64 Ez = int64(Max.Z) - Min.Z + 1;
		const int64 DenseSize  = Ex * Ey * Ez * 2;
		const int64 SparseSize = int64(Header.NumCells) * SparseBytesPerCell;

		const bool bDense = DenseSize <= SparseSize;
		const int64 RawSize = bDense ? DenseSize : SparseSize;
		if (RawSize > MaxRawSize)
		{
			UE_LOG(LogMazeForge, Warning,
				TEXT("Grid: %d cells are too many for one packed block; they are saved cell by cell."),
				Header.NumCells);
			return false;
		}

		Header.Layout = bDense ? LayoutDense : LayoutSparse;
		Header.Min = Min;
		Header.Extent = bDense ? FIntVector(int32(Ex), int32(Ey), int32(Ez)) : FIntVector::ZeroValue;
		Header.RawSize = int32(RawSize);
	}

	TArray<uint8> Raw;
	Raw.SetNumZeroed(Header.RawSize);

	if (Header.NumCells > 0 && Header.Layout == LayoutDense)
	{
		const int64 Volume = int64(Header.RawSize) / 2;
		uint8* Types = Raw.GetData();
		uint8* Palettes = Types + Volume;

		for (const TPair<FIntVector, FMazeCell>& Pair : Cells)
		{
			const int64 I = DenseIndex(Pair.Key - Header.Min, Header.Extent);
			Types[I] = static_cast<uint8>(Pair.Value.Type);
			Palettes[I] = Pair.Value.PaletteIndex;
		}
	}
	else if (Header.NumCells > 0)
	{
		TArray<FIntVector> Keys;
		Cells.GenerateKeyArray(Keys);
		Keys.Sort([](const FIntVector& A, const FIntVector& B)
		{
			if (A.Z != B.Z) { return A.Z < B.Z; }
			if (A.Y != B.Y) { return A.Y < B.Y; }
			return A.X < B.X;
		});

		const int32 N = Keys.Num();
		uint8* Xs = Raw.GetData();
		uint8* Ys = Xs + int64(N) * sizeof(int32);
		uint8* Zs = Ys + int64(N) * sizeof(int32);
		uint8* Types = Zs + int64(N) * sizeof(int32);
		uint8* Palettes = Types + N;

		for (int32 i = 0; i < N; ++i)
		{
			const FMazeCell& Cell = Cells.FindChecked(Keys[i]);
			WriteInt32(Xs, i, Keys[i].X);
			WriteInt32(Ys, i, Keys[i].Y);
			WriteInt32(Zs, i, Keys[i].Z);
			Types[i] = static_cast<uint8>(Cell.Type);
			Palettes[i] = Cell.PaletteIndex;
		}
	}

	TArray<uint8> Payload;
	if (bCompress && Header.RawSize > 0)
	{
		int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Oodle, Header.RawSize);
		Payload.SetNumUninitialized(CompressedSize);

		if (FCompression::CompressMemory(NAME_Oodle, Payload.GetData(), CompressedSize,
			Raw.GetData(), Header.RawSize))
		{
			Payload.SetNum(CompressedSize);
			Header.bCompressed = 1;
		}
	}

	if (!Header.bCompressed)
	{
		Payload = MoveTemp(Raw);
	}

	{
		FMemoryWriter Writer(PackedCells);
		Writer << Header;
	}
	PackedCells.Append(Payload);

	OutStash = MoveTemp(Cells);
	Cells.Reset();
	return true;
}

void FMazeGrid::EndPack(TMap<FIntVector, FMazeCell>& Stash)
{
	Cells = MoveTemp(Stash);
	PackedCells.Empty();
}

bool FMazeGrid::UnpackAfterLoad(FString& OutError)
{
	if (PackedCells.Num() == 0)
	{
		return true; // saved in the tagged form, Cells already hold everything
	}

	FPackHeader Header;
	FMemoryReader Reader(PackedCells);
	Reader << Header;

	if (Reader.IsError() || Header.Magic != PackMagic)
	{
		OutError = TEXT("the block has no valid header");
		return false;
	}

	if (Header.Version != PackVersion)
	{
		OutError = FString::Printf(TEXT("block version %d, this build reads %d"), Header.Version, PackVersion);
		return false;
	}

	const int64 HeaderSize = Reader.Tell();
	const int64 PayloadSize = PackedCells.Num() - HeaderSize;
	const FIntVector& E = Header.Extent;

	const bool bDense = Header.Layout == LayoutDense;
	const int64 ExpectedRaw = Header.NumCells == 0 ? 0
		: bDense ? int64(E.X) * E.Y * E.Z * 2
		: int64(Header.NumCells) * SparseBytesPerCell;

	if (Header.NumCells < 0 || Header.RawSize != ExpectedRaw || E.X < 0 || E.Y < 0 || E.Z < 0
		|| (Header.Layout != LayoutDense && Header.Layout != LayoutSparse))
	{
		OutError = TEXT("the header does not add up");
		return false;
	}

	TArray<uint8> Raw;
	if (Header.bCompressed)
	{
		Raw.SetNumUninitialized(Header.RawSize);
		if (!FCompression::UncompressMemory(NAME_Oodle, Raw.GetData(), Header.RawSize,
			PackedCells.GetData() + HeaderSize, int32(PayloadSize)))
		{
			OutError = TEXT("decompression failed");
			return false;
		}
	}
	else
	{
		if (PayloadSize != Header.RawSize)
		{
			OutError = TEXT("the block is truncated");
			return false;
		}
		Raw.Append(PackedCells.GetData() + HeaderSize, Header.RawSize);
	}

	TMap<FIntVector, FMazeCell> Decoded;
	Decoded.Reserve(Header.NumCells);

	if (Header.NumCells > 0 && bDense)
	{
		const int64 Volume = int64(Header.RawSize) / 2;
		const uint8* Types = Raw.GetData();
		const uint8* Palettes = Types + Volume;

		for (int32 Z = 0; Z < E.Z; ++Z)
		for (int32 Y = 0; Y < E.Y; ++Y)
		for (int32 X = 0; X < E.X; ++X)
		{
			const FIntVector Local(X, Y, Z);
			const int64 I = DenseIndex(Local, E);
			if (Types[I] != 0)
			{
				Decoded.Add(Header.Min + Local,
					FMazeCell(static_cast<EMazeCellType>(Types[I]), Palettes[I]));
			}
		}
	}
	else if (Header.NumCells > 0)
	{
		const int32 N = Header.NumCells;
		const uint8* Xs = Raw.GetData();
		const uint8* Ys = Xs + int64(N) * sizeof(int32);
		const uint8* Zs = Ys + int64(N) * sizeof(int32);
		const uint8* Types = Zs + int64(N) * sizeof(int32);
		const uint8* Palettes = Types + N;

		for (int32 i = 0; i < N; ++i)
		{
			Decoded.Add(FIntVector(ReadInt32(Xs, i), ReadInt32(Ys, i), ReadInt32(Zs, i)),
				FMazeCell(static_cast<EMazeCellType>(Types[i]), Palettes[i]));
		}
	}

	Cells = MoveTemp(Decoded);
	PackedCells.Empty();
	return true;
}
