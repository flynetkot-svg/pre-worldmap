#pragma once

#include "CoreMinimal.h"
#include "Generators/MazeGeneratorBase.h"
#include "MazeGenerator_Random.generated.h"

struct FMazeLayout2D;

/**
 *  A random industrial complex in the spirit of Saboteur 2.
 *
 *  The topology here is not the textbook "perfect maze" but a building: halls with
 *  floors, vertical shafts running all the way through, horizontal passages and a
 *  corridor along the roof, which the player enters from above. A perfect maze would
 *  give uniformly narrow passages, in which a side-scroller does not read.
 *
 *  The world size, the depth and the borders are deliberately NOT duplicated here as
 *  parameters — they live in FMazeGrid. Two sources of truth for one value drift apart
 *  sooner or later, and then it is anyone's guess whose size is the real one.
 */
UCLASS(DisplayName = "Random (Saboteur-style complex)")
class MAZEFORGECORE_API UMazeGenerator_Random : public UMazeGeneratorBase
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------ sectors

	/** Minimum side of a sector in cells. Below it the BSP stops splitting. */
	UPROPERTY(EditAnywhere, Category = "Sectors", meta = (ClampMin = "6", UIMax = "64"))
	int32 MinSectorCells = 22;

	/** Limit on the split depth. The larger it is, the smaller and more numerous the halls. */
	UPROPERTY(EditAnywhere, Category = "Sectors", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxSplitDepth = 4;

	/** Fraction of sectors that become halls. The rest stay solid. */
	UPROPERTY(EditAnywhere, Category = "Sectors", meta = (ClampMin = "0", ClampMax = "1"))
	float RoomChance = 0.8f;

	/** Inset of a hall from the sector borders: the wall thickness between adjacent halls. */
	UPROPERTY(EditAnywhere, Category = "Sectors", meta = (ClampMin = "1", UIMax = "8"))
	int32 RoomMargin = 2;

	// ------------------------------------------------------------------- floors

	UPROPERTY(EditAnywhere, Category = "Floors", meta = (ClampMin = "0", UIMax = "10"))
	int32 FloorsMin = 1;

	UPROPERTY(EditAnywhere, Category = "Floors", meta = (ClampMin = "0", UIMax = "10"))
	int32 FloorsMax = 4;

	/** Slab thickness in cells. */
	UPROPERTY(EditAnywhere, Category = "Floors", meta = (ClampMin = "1", UIMax = "4"))
	int32 FloorThickness = 1;

	/** Clear height of a floor. Must be greater than the character's height in cells. */
	UPROPERTY(EditAnywhere, Category = "Floors", meta = (ClampMin = "2", UIMax = "16"))
	int32 FloorGapMin = 5;

	/** Fraction of slabs that get an opening with a ladder. */
	UPROPERTY(EditAnywhere, Category = "Floors", meta = (ClampMin = "0", ClampMax = "1"))
	float LadderChance = 0.7f;

	/**
	 *  Width of the opening in a slab, in cells.
	 *
	 *  Two at minimum: the player simply will not land in a one-cell-wide opening while
	 *  falling, and even if they do — they will get stuck by the shoulders on the edges.
	 */
	UPROPERTY(EditAnywhere, Category = "Floors", meta = (ClampMin = "2", UIMax = "8"))
	int32 LadderOpeningCells = 2;

	// ------------------------------------------------------------------- shafts

	UPROPERTY(EditAnywhere, Category = "Shafts", meta = (ClampMin = "0", UIMax = "32"))
	int32 ShaftCount = 6;

	/** Minimum distance between shafts along X. */
	UPROPERTY(EditAnywhere, Category = "Shafts", meta = (ClampMin = "2", UIMax = "64"))
	int32 ShaftMinSpacing = 14;

	UPROPERTY(EditAnywhere, Category = "Shafts", meta = (ClampMin = "1", UIMax = "8"))
	int32 ShaftWidth = 2;

	/** Run the shafts up to the roof corridor. Off — a shaft ends at the topmost hall. */
	UPROPERTY(EditAnywhere, Category = "Shafts")
	bool bShaftsReachTop = true;

	// ----------------------------------------------------------------- passages

	/** Height of a horizontal passage in cells. */
	UPROPERTY(EditAnywhere, Category = "Corridors", meta = (ClampMin = "2", UIMax = "12"))
	int32 CorridorHeight = 4;

	/**
	 *  A corridor along the top edge of the world.
	 *
	 *  This is the landing area: the player enters the maze from above and needs
	 *  somewhere to land and somewhere to start the descent from. It also ties the
	 *  tops of all the shafts together.
	 */
	UPROPERTY(EditAnywhere, Category = "Corridors")
	bool bCarveRoofCorridor = true;

	// ------------------------------------------------------------- connectivity

	/**
	 *  The player's height in cells. The main connectivity parameter.
	 *
	 *  Everything is computed and carved for an agent of this height: a passage lower
	 *  than it is a wall to the player, no matter how much success the flood fill
	 *  reports. The stock UE capsule is 176 cm, that is two cells of 100.
	 */
	UPROPERTY(EditAnywhere, Category = "Connectivity", meta = (ClampMin = "1", UIMax = "6"))
	int32 AgentHeightCells = 2;

	/**
	 *  Carve a way through to cut-off cavities instead of filling them back in.
	 *
	 *  The flag off gives a denser solid, the flag on gives more routes.
	 */
	UPROPERTY(EditAnywhere, Category = "Connectivity")
	bool bCarveIslands = true;

	/** Cavities smaller than this are always filled: there is no point carving to a closet. */
	UPROPERTY(EditAnywhere, Category = "Connectivity", meta = (ClampMin = "1", UIMax = "200"))
	int32 MinIslandCells = 16;

	virtual FText GetDisplayName() const override;

protected:
	virtual void Generate(FMazeGrid& InOutGrid, FRandomStream& Rng) override;

private:
	/** Sector rectangle in XZ. Min inclusive, Max exclusive. */
	struct FSector
	{
		FIntPoint Min = FIntPoint::ZeroValue;
		FIntPoint Max = FIntPoint::ZeroValue;

		int32 Width() const { return Max.X - Min.X; }
		int32 Height() const { return Max.Y - Min.Y; }
	};

	void SplitSectors(const FSector& Sector, int32 Depth, FRandomStream& Rng,
	                  TArray<FSector>& OutSectors) const;

	void CarveRooms(FMazeLayout2D& Layout, const TArray<FSector>& Sectors,
	                FRandomStream& Rng, TArray<FSector>& OutRooms) const;

	void CarveFloors(FMazeLayout2D& Layout, const TArray<FSector>& Rooms,
	                 FRandomStream& Rng) const;

	void CarveShafts(FMazeLayout2D& Layout, const FSector& Area, int32 RoofZ,
	                 FRandomStream& Rng, TArray<int32>& OutShaftX) const;

	void CarveCorridors(FMazeLayout2D& Layout, const TArray<FSector>& Rooms,
	                    const TArray<int32>& ShaftX) const;

	/** The entry point from above. Returns (INDEX_NONE, INDEX_NONE) if there is nowhere to put it. */
	FIntPoint FindEntryPoint(const FMazeLayout2D& Layout, int32 RoofZ) const;

	int32 EnsureConnectivity(FMazeLayout2D& Layout, const FIntPoint& Entry) const;

	/** A straight cut between two points. Used for carving through to cavities. */
	void CarveLine(FMazeLayout2D& Layout, const FIntPoint& From, const FIntPoint& To) const;
};
