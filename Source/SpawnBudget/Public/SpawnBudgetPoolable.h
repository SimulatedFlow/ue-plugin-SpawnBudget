// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SpawnBudgetPoolable.generated.h"

class ASpawnSource;

UINTERFACE(MinimalAPI, Blueprintable, meta = (DisplayName = "Spawn Budget Poolable"))
class USpawnBudgetPoolable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implement this on an actor that has state a pool cannot reset on its own.
 *
 * The pool already restores everything the engine knows about: transform, visibility, collision, the
 * actor's tick and the tick of every component that was running when it was parked. What it cannot know
 * is what the actor means - health back to full, a magazine refilled, a state machine back to Idle, a
 * material parameter that was set while it was dying, a timer that was running. That is what these two
 * calls are for.
 *
 * Both are BlueprintNativeEvents, so an actor can answer them in C++, in Blueprint, or in both.
 *
 * An actor that implements nothing still pools correctly - it simply comes back exactly as it went away.
 * That is right for a rock and wrong for an enemy, and the plugin cannot tell the difference, so it asks.
 */
class SPAWNBUDGET_API ISpawnBudgetPoolable
{
	GENERATED_BODY()

public:
	/**
	 * Taken out of the pool and put into the world at its new transform, immediately before it is shown.
	 *
	 * Reset gameplay state here. Called for a brand new actor as well as for a reused one, so there is
	 * exactly one code path to get right rather than two that have to agree.
	 *
	 * @param Source The spawn source that asked for it, or null when the request came from Blueprint.
	 * @param bWasPooled True when this actor is being reused, false when it was just constructed.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SpawnBudget|Pooling")
	void OnSpawnedFromPool(ASpawnSource* Source, bool bWasPooled);
	virtual void OnSpawnedFromPool_Implementation(ASpawnSource* Source, bool bWasPooled) {}

	/**
	 * About to be parked: still valid, still in the world, not yet hidden.
	 *
	 * Stop anything the engine will not stop for you - a latent action, a sound, an attached actor you
	 * spawned yourself. Do not destroy the actor here; the pool is about to take ownership of it.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SpawnBudget|Pooling")
	void OnReturnedToPool();
	virtual void OnReturnedToPool_Implementation() {}
};
