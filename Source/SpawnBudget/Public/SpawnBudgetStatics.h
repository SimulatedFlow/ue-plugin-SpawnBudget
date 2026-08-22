// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"
#include "SpawnBudgetTypes.h"
#include "SpawnBudgetStatics.generated.h"

class AActor;
class ASpawnSource;

/**
 * The whole plugin from Blueprint, without a reference to the subsystem in sight.
 *
 * Every call is world-context-aware and safe in a world that has no SpawnBudget subsystem at all: queries
 * answer nothing, setters do nothing, and nobody crashes. That matters more than it sounds - it means a
 * Blueprint written against SpawnBudget still runs in a test map where no source was ever placed, and a
 * debug panel wired to these functions can be dropped into any level without a null check on every node.
 */
UCLASS(meta = (DisplayName = "SpawnBudget"))
class SPAWNBUDGET_API USpawnBudgetStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ Requests -----------------------------------------------------------------------------------------

	/**
	 * Ask for an actor. False means the request was refused rather than queued.
	 *
	 * The actor does not exist when this returns, and that is the point - it exists when the budget can
	 * afford it. Bind to OnActorSpawned, or read the population, if something has to happen at that moment.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "3"))
	static bool RequestSpawn(const UObject* WorldContextObject, TSubclassOf<AActor> ActorClass, const FTransform& Transform, float Weight = 1.0f);

	/** Hand an actor back to the plugin. Safe for an actor it never spawned. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static void ReleaseActor(const UObject* WorldContextObject, AActor* Actor);

	/** Throw Count extra requests at the queue, spread over every source. Returns how many were accepted. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static int32 RequestStress(const UObject* WorldContextObject, int32 Count = 1000);

	/** Throw the waiting queue away. Nothing already alive is touched. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static void FlushQueue(const UObject* WorldContextObject);

	/** Despawn everything, empty both queues and destroy every parked actor. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static void ClearAll(const UObject* WorldContextObject);

	//~ Budget -------------------------------------------------------------------------------------------

	/** Change what one frame may cost. A negative value leaves that ceiling alone. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static void SetBudget(const UObject* WorldContextObject, int32 MaxSpawnsPerFrame, int32 MaxDespawnsPerFrame = -1, float MaxMillisecondsPerFrame = -1.0f);

	/** The budget in force. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static FSpawnBudgetLimits GetBudget(const UObject* WorldContextObject);

	/** The world population ceiling. Lowering it despawns the excess, furthest away first. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static void SetMaxPopulation(const UObject* WorldContextObject, int32 MaxPopulation);

	/** The world population ceiling. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static int32 GetMaxPopulation(const UObject* WorldContextObject);

	/** Actors alive and owned by the plugin. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static int32 GetPopulation(const UObject* WorldContextObject);

	/** Everything still waiting - spawns plus despawns. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static int32 GetQueueLength(const UObject* WorldContextObject);

	/** Requests waiting to be spawned. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static int32 GetSpawnQueueLength(const UObject* WorldContextObject);

	/** Actors waiting to be despawned. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static int32 GetDespawnQueueLength(const UObject* WorldContextObject);

	//~ Switches -----------------------------------------------------------------------------------------

	/** Park despawned actors and reuse them, or destroy them. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static void SetPoolingEnabled(const UObject* WorldContextObject, bool bEnabled);

	/** Whether despawned actors are parked or destroyed. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static bool IsPoolingEnabled(const UObject* WorldContextObject);

	/** Multiply every source's rings by this, without touching a single source. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static void SetRingScale(const UObject* WorldContextObject, float RingScale);

	/** The multiplier currently applied to every source's rings. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static float GetRingScale(const UObject* WorldContextObject);

	/** Run the spawner at all. Off keeps the sources, the queue and the statistics box. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static void SetSpawningEnabled(const UObject* WorldContextObject, bool bEnabled);

	/** Whether the spawner is running. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static bool IsSpawningEnabled(const UObject* WorldContextObject);

	/** Draw every source's three rings and the queued spawn points. Compiled out of a Shipping build. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static void SetDebugDraw(const UObject* WorldContextObject, bool bEnabled);

	//~ Statistics ---------------------------------------------------------------------------------------

	/** Everything the statistics box draws. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static FSpawnBudgetStats GetStats(const UObject* WorldContextObject);

	/** Spawns served from a pool since the world started. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static int32 GetPoolHits(const UObject* WorldContextObject);

	/** Spawns that had to build a new actor. The number that must stand still while pooling is on. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static int32 GetNewAllocations(const UObject* WorldContextObject);

	/** Requests turned away by the population ceiling or the queue ceiling. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static int32 GetRefusedCount(const UObject* WorldContextObject);

	//~ Sources ------------------------------------------------------------------------------------------

	/** Every source registered in this world. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static void GetSources(const UObject* WorldContextObject, TArray<ASpawnSource*>& OutSources);

	/** Queue everything one source placed for despawn, at the usual budget. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (WorldContext = "WorldContextObject"))
	static void DespawnSource(const UObject* WorldContextObject, ASpawnSource* Source);
};
