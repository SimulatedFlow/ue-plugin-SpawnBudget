// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "SpawnBudgetTypes.h"
#include "SpawnBudgetPool.generated.h"

class AActor;
class ASpawnSource;
class UWorld;

/**
 * The parked actors of exactly one class, and the two numbers that say whether pooling is working.
 *
 * A despawn does not destroy: it tells the actor it is going away, switches off its tick and the tick of
 * every component that was running, hides it, takes its collision away and parks it. A spawn does the
 * same in reverse and puts it back where it is wanted. Nothing is constructed, no Blueprint construction
 * script runs, no components are registered, and the garbage collector never hears about it.
 *
 * The pair to watch is PoolHits against NewAllocations. NewAllocations counts the times this pool had to
 * build an actor from nothing, which is the expensive path - the one that spikes a frame. In a world that
 * has reached its steady state that number must stop moving while PoolHits keeps climbing. If it does not,
 * either the pool ceiling is too low for the population, or something is destroying actors behind the
 * plugin's back. Both are worth knowing, and neither is visible from a frame rate graph.
 *
 * One pool per class rather than one pool with a class field: an actor can only ever be reused as itself,
 * so a shared pool would be a map lookup on every single acquire, and a per-class ceiling would be
 * impossible to express.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Spawn Budget Pool"))
class SPAWNBUDGET_API USpawnBudgetPool : public UObject
{
	GENERATED_BODY()

public:
	/** Called once by the subsystem, immediately after construction. */
	void Initialise(TSubclassOf<AActor> InActorClass, int32 InMaxPooled, bool bInParkPooledActors, const FVector& InParkLocation);

	/** Apply changed project settings to a pool that already exists. */
	void ApplyLimits(int32 InMaxPooled, bool bInParkPooledActors, const FVector& InParkLocation);

	/**
	 * Take an actor out of the pool, or build one if the pool is empty.
	 *
	 * @param World         World to spawn into when nothing can be reused.
	 * @param Transform     Where it goes.
	 * @param Owner         Actor ownership for the new actor. May be null.
	 * @param Source        Passed on to ISpawnBudgetPoolable::OnSpawnedFromPool. May be null.
	 * @param bOutPoolHit   True when an existing actor was reused rather than built.
	 * @return The live actor, or null if the class could not be spawned at all.
	 */
	AActor* Acquire(UWorld* World, const FTransform& Transform, AActor* Owner, ASpawnSource* Source, bool& bOutPoolHit);

	/**
	 * Park an actor. Returns false when the pool was full and the actor has to be destroyed instead.
	 *
	 * The actor is left valid and untouched on a false return - deciding to destroy it is the caller's
	 * job, because the caller is the one keeping the population count.
	 */
	bool Release(AActor* Actor);

	/** Destroy everything parked here. Called on world teardown and by Spawn.Clear. */
	void DestroyParked();

	/** Actors parked and available for reuse. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget|Pool")
	int32 GetParkedCount() const { return Parked.Num(); }

	/** Spawns this pool served from a parked actor. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget|Pool")
	int32 GetPoolHits() const { return PoolHits; }

	/** Spawns this pool had to build from nothing. The number that must stand still. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget|Pool")
	int32 GetNewAllocations() const { return NewAllocations; }

	/** Actors this pool destroyed because it was already full. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget|Pool")
	int32 GetOverflowDestroyed() const { return OverflowDestroyed; }

	/** What this pool holds. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget|Pool")
	TSubclassOf<AActor> GetActorClass() const { return ActorClass; }

private:
	/** Hide, decollide, stop ticking, remember what was ticking, and put it away. */
	void Deactivate(AActor* Actor, FSpawnBudgetParkedActor& OutEntry) const;

	/** Put it back where it belongs, switch on exactly what was switched off, and show it. */
	void Reactivate(const FSpawnBudgetParkedActor& Entry, const FTransform& Transform) const;

	UPROPERTY()
	TSubclassOf<AActor> ActorClass;

	UPROPERTY()
	TArray<FSpawnBudgetParkedActor> Parked;

	int32 MaxPooled = 256;
	bool bParkPooledActors = true;
	FVector ParkLocation = FVector(0.0f, 0.0f, -100000.0f);

	int32 PoolHits = 0;
	int32 NewAllocations = 0;
	int32 OverflowDestroyed = 0;
};
