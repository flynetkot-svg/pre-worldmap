#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MazeStreamTypes.h"
#include "MazeStreamingSubsystem.generated.h"

class UMazeRoomPool;
class UMazeStreamingComponent;
class UMazeWorldManifest;

/**
 *  The manager of the level load pool.
 *
 *  The only thing the runtime knows about the maze is the manifest. The voxel grid, the
 *  slicers and the whole editor pipeline stay in the editor and never reach the game build.
 */
UCLASS()
class MAZEFORGESTREAMING_API UMazeStreamingSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterObserver(UMazeStreamingComponent* Observer);
	void UnregisterObserver(UMazeStreamingComponent* Observer);

	UMazeRoomPool* GetPool() const { return Pool; }

	// UTickableWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UPROPERTY()
	TObjectPtr<UMazeRoomPool> Pool;

	UPROPERTY()
	TWeakObjectPtr<UMazeStreamingComponent> Observer;

	/**
	 *  The manifest the pool is currently working on.
	 *
	 *  Compared every tick, because the observer's Manifest field is writable at runtime — that
	 *  is how a door moves the player from one maze to the next. Everything the pool knows is
	 *  about one manifest: its rooms, its streaming entries, its camera bounds. When the
	 *  manifest changes, all of it has to go, and the going is what unloads the old maze.
	 */
	UPROPERTY()
	TWeakObjectPtr<UMazeWorldManifest> ActiveManifest;

	float TimeSinceUpdate = 0.0f;
	bool bCameraBoundsApplied = false;

	/**
	 *  Whether the reason for streaming nothing has already been said.
	 *
	 *  The check that stops the tick has three fatal branches and used to take all three in
	 *  silence, once per frame, for ever. It is cleared again as soon as streaming works, so a
	 *  break that happens later still gets its line.
	 */
	bool bReportedIdleReason = false;

	void DrawDebug(const class UMazeWorldManifest& Manifest, const FMazeStreamQuery& Query) const;
};
