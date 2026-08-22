// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpawnBudgetTypes.h"
#include "SpawnSource.generated.h"

class USceneComponent;
class USplineComponent;

/**
 * A place in the level that wants a population, and the rules for keeping it.
 *
 * Drag one in, give it a class or three, set how many of them there should be and how far away that stops
 * mattering. Nothing else has to be wired: no Blueprint, no trigger, no timer. While the player is inside
 * the SpawnIn ring the source keeps itself topped up to MaxAlive; when the player leaves, its actors are
 * taken back one distance ring later.
 *
 * Three things are worth knowing about how it is put together.
 *
 * **It does not tick.** Not one of these has a tick function, and none of them ever calls SpawnActor. The
 * world subsystem walks every source once per frame, decides what should exist, and puts requests in one
 * queue that one budget drains. That is the only way a hundred sources can cost what one costs, and the
 * only way a level that suddenly needs four hundred actors does not stall on the frame it needs them.
 *
 * **It asks rather than does.** A source getting what it asked for is not guaranteed, and that is the
 * design rather than a limitation: the frame budget, the world population ceiling and the queue ahead of
 * it all come first. A source that could spawn directly could ruin a frame on its own, and no amount of
 * budgeting elsewhere could stop it.
 *
 * **A scatter point that would be born dead is not used.** A point that lands outside the despawn ring,
 * or off the navmesh, is drawn again rather than spawned - otherwise an actor would be created and taken
 * away again within the second, paying full price for both, and the pool would churn for nothing.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Spawn Source"))
class SPAWNBUDGET_API ASpawnSource : public AActor
{
	GENERATED_BODY()

public:
	ASpawnSource();

	//~ AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	//~ What ---------------------------------------------------------------------------------------------

	/**
	 * The classes this source spawns, with how often each is picked relative to the others.
	 *
	 * An empty list, or a list of entries with no class, is not an error - the source simply registers,
	 * reports itself on the statistics box and asks for nothing. That is what a source being built looks
	 * like, and it should not spam a log.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|What")
	TArray<FSpawnBudgetClassEntry> Classes;

	/** How many actors this source wants alive while the player is inside its SpawnIn ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|What", meta = (ClampMin = "0", UIMax = "512"))
	int32 MaxAlive = 24;

	/**
	 * How important this source's requests are, against every other source competing for the same budget.
	 *
	 * The queue is sorted by distance divided by this number, so 2.0 makes a source behave as though it
	 * were half as far away. Use it for the things a player would notice missing - the ambush, the boss's
	 * escort - and leave scenery at 1.0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|What",
		meta = (ClampMin = "0.01", UIMax = "10.0"))
	float Weight = 1.0f;

	/**
	 * Take this source out of the running without deleting it or moving it out of the level.
	 *
	 * Switching it off stops it asking for anything. It does not take away what it already placed - those
	 * actors leave through their despawn ring like any other, so switching a source off mid-play looks
	 * like the area emptying rather than like a hard cut.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|What")
	bool bSourceEnabled = true;

	//~ Where --------------------------------------------------------------------------------------------

	/** How spawn points are spread around this source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|Where")
	ESpawnScatterShape Scatter = ESpawnScatterShape::Circle;

	/** Radius for Circle, and sideways offset for Spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|Where",
		meta = (ClampMin = "0.0", UIMax = "20000.0", ForceUnits = "cm"))
	float ScatterRadius = 1500.0f;

	/** Half-size for Box, in the source's own space, so rotating the source rotates the box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|Where", meta = (ForceUnits = "cm"))
	FVector ScatterExtent = FVector(1500.0f, 1500.0f, 0.0f);

	/** What a scatter point is snapped to before an actor is put on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|Where")
	ESpawnGroundMode GroundMode = ESpawnGroundMode::LineTrace;

	/** How far up the ground trace starts and how far down it goes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|Where",
		meta = (ClampMin = "1.0", UIMax = "50000.0", ForceUnits = "cm"))
	float GroundTraceHeight = 2000.0f;

	/** How far off a navmesh a point may be and still be pulled onto it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|Where",
		meta = (ClampMin = "1.0", UIMax = "5000.0", ForceUnits = "cm"))
	float NavProjectionExtent = 300.0f;

	/** Lift every spawn point by this much after it has been snapped, so a capsule does not start inside the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|Where",
		meta = (UIMin = "0.0", UIMax = "500.0", ForceUnits = "cm"))
	float SpawnHeightOffset = 0.0f;

	/** Give each spawn a random yaw. Off keeps the source's own rotation, which is what a line of soldiers wants. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|Where")
	bool bRandomYaw = true;

	//~ When ---------------------------------------------------------------------------------------------

	/** The three distances that decide this source's population. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|When")
	FSpawnBudgetRings Rings;

	/**
	 * Shortest gap between two batches of requests from this source, in seconds. 0 asks as soon as it can.
	 *
	 * This is a drip feed, not a budget: the budget decides how fast the world may fill, and this decides
	 * whether this particular source is allowed to be the one filling it. Useful for a spawner that should
	 * trickle rather than restock instantly the moment something dies.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|When",
		meta = (ClampMin = "0.0", UIMax = "60.0", ForceUnits = "s"))
	float RefillIntervalSeconds = 0.0f;

	/** How many requests this source may add in one frame, so one source cannot fill the whole queue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|When", meta = (ClampMin = "1", UIMax = "256"))
	int32 MaxRequestsPerEvaluation = 16;

	//~ Queries ------------------------------------------------------------------------------------------

	/** How many actors from this source are alive right now. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	int32 GetLiveCount() const { return LiveCount; }

	/** How many of this source's requests are still waiting in the queue. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	int32 GetPendingCount() const { return PendingCount; }

	/** Distance from this source to the nearest viewer as of the last evaluation, or -1 before the first. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	float GetLastViewerDistance() const { return LastViewerDistance; }

	/** True when the last evaluation found this source inside its own KeepAlive ring. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	bool IsInRange() const { return bInRange; }

	/** Switch the source on or off at runtime. Does not take away what it already placed. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void SetSourceEnabled(bool bNewEnabled);

	/** Queue every actor this source placed for despawn, at the usual budget. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void DespawnAll();

	/** Pick a class according to the entries' pick weights. Null when there is nothing to pick. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	TSubclassOf<AActor> PickClass() const;

	/**
	 * Draw one spawn point for this source.
	 *
	 * @param OutTransform   Where an actor would go.
	 * @return False when no usable point could be found within the configured number of attempts.
	 */
	bool BuildSpawnTransform(FTransform& OutTransform, const TArray<FVector>& ViewerLocations, float RingScale) const;

	/** The spline scatter shape follows this. Empty by default; add points to use ESpawnScatterShape::Spline. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpawnBudget")
	TObjectPtr<USplineComponent> ScatterSpline;

private:
	friend class USpawnBudgetSubsystem;

	/** Snap a candidate point to the ground or the navmesh. False when it landed on nothing usable. */
	bool ProjectPoint(FVector& InOutPoint) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpawnBudget", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** Maintained by the subsystem, which is the only thing that spawns or despawns. */
	int32 LiveCount = 0;
	int32 PendingCount = 0;
	float LastViewerDistance = -1.0f;
	float TimeUntilRefill = 0.0f;
	bool bInRange = false;
};
