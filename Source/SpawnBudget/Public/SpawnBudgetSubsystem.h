// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "SpawnBudgetTypes.h"
#include "SpawnBudgetSubsystem.generated.h"

class AActor;
class AHUD;
class ASpawnSource;
class UCanvas;
class USpawnBudgetPool;

/** Fired the moment an actor has been put into the world, pooled or not. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSpawnBudgetSpawned, AActor*, Actor, ASpawnSource*, Source);

/** Fired while the actor is still valid, immediately before it is parked or destroyed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSpawnBudgetDespawned, AActor*, Actor, ASpawnSource*, Source);

/** One thing the world is being populated for. Plain data, one per local player camera. */
struct FSpawnBudgetViewer
{
	FVector Location = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	float CosHalfFov = 0.0f;
};

/**
 * The whole plugin, running once per frame in one place.
 *
 * Its job in one sentence: work out what should exist near the player, put the difference in a queue
 * sorted by importance, and spend no more than a fixed number of spawns, despawns and milliseconds per
 * frame getting there.
 *
 * **The queue is the product.** A spawner that spawns everything that is due is easy to write and stalls
 * the moment a level asks for four hundred actors at once. Here, what is due goes into a queue sorted by
 * distance divided by the weight of the source that asked - nearest and most important first - and each
 * frame drains as much of it as the budget allows. When the millisecond ceiling runs out mid-frame the
 * loop stops where it is and continues from exactly there on the next one. Nothing is lost, nothing is
 * done twice, and the queue is still sorted, so the thing that mattered most is still first.
 *
 * **A despawn is not a destroy.** It hides the actor, takes it and its components off the tick, removes
 * its collision and parks it in a per-class pool. The next spawn of that class takes it back out. The
 * counter that proves it is PoolHits against NewAllocations, and in the steady state the second one stops
 * moving. That is the difference between a world that costs the same in minute ten as in minute one and
 * one that does not, and it is on screen rather than in a claim.
 *
 * **Three rings, not one radius.** SpawnIn fills a source, KeepAlive holds it, and an actor is only taken
 * back once it is past Despawn. The gap between the first and the last is the hysteresis, so a player
 * pacing over a boundary does not produce a spawn/despawn cycle - and it is measured in centimetres of
 * level rather than in frames of smoothing, which means it can be reasoned about on a map.
 *
 * Game and PIE only. An editor viewport is the level designer's; filling it with wandering actors while
 * somebody is building the level is the sort of help nobody asked for.
 */
UCLASS(meta = (DisplayName = "Spawn Budget Subsystem"))
class SPAWNBUDGET_API USpawnBudgetSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ UWorldSubsystem interface
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	//~ FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The subsystem for whatever world the context object belongs to, or null. */
	static USpawnBudgetSubsystem* Get(const UObject* WorldContextObject);

	//~ Sources ------------------------------------------------------------------------------------------

	/** Called by ASpawnSource on BeginPlay. Harmless to call twice. */
	void RegisterSource(ASpawnSource* Source);

	/** Called by ASpawnSource on EndPlay. What it placed stays alive and leaves through its rings. */
	void UnregisterSource(ASpawnSource* Source);

	/** Every source registered in this world, in registration order. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	void GetSources(TArray<ASpawnSource*>& OutSources) const;

	/** Queue everything one source placed for despawn, at the usual budget. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void DespawnSource(ASpawnSource* Source);

	//~ Requests -----------------------------------------------------------------------------------------

	/**
	 * Ask for an actor. Returns false when the request was refused rather than queued.
	 *
	 * Refused means one of two things and both are counted: the world is already at its population
	 * ceiling, or the queue is at its own. A queued request is a promise; refusing one out loud is
	 * honest, and dropping one silently is the bug that gets found six weeks later in a bug report about
	 * an empty village.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget", meta = (AdvancedDisplay = "2"))
	bool RequestSpawn(TSubclassOf<AActor> ActorClass, const FTransform& Transform, float Weight = 1.0f);

	/** The same request, attributed to a source so that its rings, weight and live count apply. */
	bool RequestSpawnFromSource(TSubclassOf<AActor> ActorClass, const FTransform& Transform, ASpawnSource* Source);

	/**
	 * Hand an actor back. It is queued for despawn and taken away within the despawn budget.
	 *
	 * Safe for an actor this plugin never spawned - it is simply not ours, nothing happens, and no
	 * warning is printed, because a game calling this from a shared death handler is doing the right
	 * thing rather than a wrong one.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void ReleaseActor(AActor* Actor);

	/** Enqueue Count extra requests, spread over the registered sources. What Spawn.Stress runs. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	int32 RequestStress(int32 Count);

	/** Throw the waiting queue away. Nothing that is already alive is touched. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void FlushQueue();

	/** Despawn everything, empty the queues, and destroy every parked actor. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void ClearAll();

	//~ Budget -------------------------------------------------------------------------------------------

	/** Change what one frame may cost. Negative values leave that ceiling alone. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void SetBudget(int32 MaxSpawnsPerFrame, int32 MaxDespawnsPerFrame, float MaxMillisecondsPerFrame);

	/** The budget in force. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	FSpawnBudgetLimits GetBudget() const { return Limits; }

	/** The world-wide ceiling. Lowering it below the current population queues the excess, furthest first. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void SetMaxPopulation(int32 NewMaxPopulation);

	/** The world-wide ceiling. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	int32 GetMaxPopulation() const { return MaxPopulation; }

	/** Actors alive and owned by the plugin. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	int32 GetPopulation() const { return LiveActors.Num(); }

	/** Everything still waiting - spawns plus despawns. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	int32 GetQueueLength() const { return SpawnQueue.Num() + DespawnQueue.Num(); }

	/** Requests waiting to be spawned. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	int32 GetSpawnQueueLength() const { return SpawnQueue.Num(); }

	/** Actors waiting to be despawned. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	int32 GetDespawnQueueLength() const { return DespawnQueue.Num(); }

	//~ Switches -----------------------------------------------------------------------------------------

	/** Park despawned actors and reuse them, or destroy them. The one click the whole product turns on. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void SetPoolingEnabled(bool bNewEnabled);

	/** Whether despawned actors are parked or destroyed. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	bool IsPoolingEnabled() const { return bPoolingEnabled; }

	/**
	 * Multiply every source's rings by this, without touching a single source.
	 *
	 * One number for the whole world, so a quality setting, a platform profile or a debug button can pull
	 * the population in without a designer having had to expose a scalar on every actor in the level.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void SetRingScale(float NewRingScale);

	/** The multiplier currently applied to every source's rings. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	float GetRingScale() const { return RingScale; }

	/** Run the spawner at all. Off keeps the sources, the queue and the statistics box. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void SetEnabled(bool bNewEnabled);

	/** Whether the spawner is running. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	bool IsEnabled() const { return bEnabled; }

	/** Draw the rings and the queued spawn points. Compiled out of a Shipping build. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget")
	void SetDebugDraw(bool bNewEnabled);

	/** Whether the rings are being drawn. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	bool IsDebugDrawEnabled() const { return bDebugDraw; }

	//~ Statistics ---------------------------------------------------------------------------------------

	/** Everything the statistics box draws, as of the last tick. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	FSpawnBudgetStats GetStats() const { return Stats; }

	/** Spawns served from a pool since the world started. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	int32 GetPoolHits() const { return Stats.PoolHits; }

	/** Spawns that had to build a new actor. The number that must stand still. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	int32 GetNewAllocations() const { return Stats.NewAllocations; }

	/** Requests turned away by the population ceiling or the queue ceiling. */
	UFUNCTION(BlueprintPure, Category = "SpawnBudget")
	int32 GetRefusedCount() const { return RefusedCount; }

	/** Draw the statistics box at this corner and width. Called by ASpawnBudgetHUD. */
	void DrawStatsBox(UCanvas* Canvas, const FVector2D& Origin, float Width) const;

	/** How many lines the box will draw, so a caller can size a background around it. */
	int32 GetStatsLineCount() const;

	/** Print the same numbers to the log. What Spawn.Stats runs. */
	void LogStats() const;

	/** Print every source with its distance, live count and rings. */
	void LogSources() const;

	//~ Events -------------------------------------------------------------------------------------------

	/** An actor has just been put into the world. */
	UPROPERTY(BlueprintAssignable, Category = "SpawnBudget|Events")
	FSpawnBudgetSpawned OnActorSpawned;

	/** An actor is about to be parked or destroyed, and is still valid. */
	UPROPERTY(BlueprintAssignable, Category = "SpawnBudget|Events")
	FSpawnBudgetDespawned OnActorDespawned;

private:
	//~ Tick stages
	void ApplySettings();
	void UpdateViewers();
	void EvaluateSources(float DeltaTime);
	void ScanLiveActors();
	void RescoreSpawnQueue(float DeltaTime);
	void ServiceQueues();

	/** The one door every request goes through, from Blueprint, from a source and from Spawn.Stress alike. */
	bool EnqueueSpawn(TSubclassOf<AActor> ActorClass, const FTransform& Transform, ASpawnSource* Source, float Weight, const FSpawnBudgetRings& Rings);

	/** Take a live entry out of the list and keep the index map straight. */
	void RemoveLiveAt(int32 Index);

	/** Recompute everything the statistics box reads. */
	void UpdateStats(double TickStartSeconds);

	/** One item off the front of the spawn queue. False when it was refused rather than spawned. */
	bool ServiceOneSpawn();

	/** One item off the front of the despawn queue. False when the entry was already gone. */
	bool ServiceOneDespawn();

	/** Park or destroy, and take it out of the live list. */
	void DespawnLiveActor(int32 LiveIndex);

	/** Put an actor in the despawn queue once, never twice. */
	void QueueDespawn(int32 LiveIndex, float Score);

	/** Distance from a point to the nearest viewer, and whether it is inside a camera cone. */
	float DistanceToNearestViewer(const FVector& Point, bool* bOutVisible = nullptr) const;

	/** The pool for a class, made on first use. */
	USpawnBudgetPool* FindOrCreatePool(TSubclassOf<AActor> ActorClass);

	/** Score for a queued request: distance divided by weight, halved again when it is on screen. */
	float ComputeScore(const FVector& Location, float Weight, bool& bOutVisible) const;

	void RebindHudDelegate();
	void OnAnyHUDPostRender(AHUD* HUD, UCanvas* Canvas);

	/**
	 * Draw the rings and the queued spawn points.
	 *
	 * Declared unconditionally on purpose. ENABLE_DRAW_DEBUG is an engine define that a header cannot rely
	 * on having seen, so guarding a declaration with it gives one translation unit a class with the method
	 * and another a class without - which links, and then does not. The guard belongs around the body.
	 */
	void DrawDebugRings() const;

	//~ State --------------------------------------------------------------------------------------------

	UPROPERTY()
	TArray<TWeakObjectPtr<ASpawnSource>> Sources;

	UPROPERTY()
	TArray<FSpawnBudgetLiveActor> LiveActors;

	UPROPERTY()
	TArray<FSpawnBudgetPendingSpawn> SpawnQueue;

	UPROPERTY()
	TArray<FSpawnBudgetPendingDespawn> DespawnQueue;

	UPROPERTY()
	TMap<TSubclassOf<AActor>, TObjectPtr<USpawnBudgetPool>> Pools;

	/** Index into LiveActors, so releasing a named actor does not walk the whole population. */
	TMap<FObjectKey, int32> LiveIndexByActor;

	TArray<FSpawnBudgetViewer> Viewers;

	FSpawnBudgetLimits Limits;
	int32 MaxPopulation = 2000;
	int32 MaxQueueLength = 8192;
	int32 MaxLiveScansPerFrame = 1024;
	float RescoreIntervalSeconds = 0.25f;
	float VisibleWeightMultiplier = 2.0f;
	int32 MaxPooledPerClass = 256;
	bool bParkPooledActors = true;
	FVector ParkLocation = FVector(0.0f, 0.0f, -100000.0f);
	FSpawnBudgetRings DefaultRings;

	bool bEnabled = true;
	bool bPoolingEnabled = true;
	bool bDebugDraw = false;
	bool bAutoDrawStatsOnAnyHUD = false;
	float RingScale = 1.0f;

	/** Where the rolling live-actor scan got to last frame. */
	int32 LiveScanCursor = 0;

	/** Arrival order for queue ties. */
	int32 NextSerial = 1;

	/** The queues have entries that have not been sorted in yet. */
	bool bSpawnQueueDirty = false;
	bool bDespawnQueueDirty = false;

	/** Spawns and destroys that went round the pools, because pooling was switched off. */
	int32 DirectAllocations = 0;
	int32 DirectDestroys = 0;

	float TimeUntilRescore = 0.0f;

	int32 RefusedCount = 0;
	int32 TotalSpawned = 0;
	int32 TotalDespawned = 0;
	int32 FlushedCount = 0;

	mutable FSpawnBudgetStats Stats;

	/** Frame the box was last drawn on, so the HUD delegate cannot stack a second one on top of it. */
	mutable uint64 LastStatsDrawFrame = 0;

	FDelegateHandle HudPostRenderHandle;
};
