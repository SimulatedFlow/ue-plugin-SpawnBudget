// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectKey.h"
#include "UObject/ObjectPtr.h"
#include "SpawnBudgetTypes.generated.h"

class AActor;
class ASpawnSource;
class UActorComponent;

/** How a source spreads its spawn points around itself. */
UENUM(BlueprintType)
enum class ESpawnScatterShape : uint8
{
	/** Everything on the source's own transform. Sentries, turrets, one-off props. */
	Point		UMETA(DisplayName = "Point"),

	/** Uniformly inside a disc of ScatterRadius. The default, and what a crowd or a herd wants. */
	Circle		UMETA(DisplayName = "Circle"),

	/** Uniformly inside a box of ScatterExtent, in the source's own space, so a rotated source rotates its box. */
	Box			UMETA(DisplayName = "Box"),

	/** Along the source's spline, offset sideways by up to ScatterRadius. Roads, corridors, fence lines, patrol routes. */
	Spline		UMETA(DisplayName = "Spline"),
};

/** What a scatter point is snapped to before an actor is put on it. */
UENUM(BlueprintType)
enum class ESpawnGroundMode : uint8
{
	/** Use the scatter point as it is. Flying things, and floors that are known to be flat. */
	None		UMETA(DisplayName = "None"),

	/** One downward line trace. Cheap, works without a navmesh, and lands on whatever is actually there. */
	LineTrace	UMETA(DisplayName = "Line Trace"),

	/** Project onto the navigation mesh, so nothing is spawned where nothing could walk. Falls back to LineTrace when there is no navmesh. */
	NavMesh		UMETA(DisplayName = "Nav Mesh"),
};

/**
 * The three distances that decide a source's population.
 *
 * They are not one radius with a fudge factor; the gap between them is the entire point.
 *
 *   dist <= SpawnIn                  the source fills itself up to MaxAlive
 *   SpawnIn < dist <= KeepAlive      it holds - nothing new is added, nothing is taken away
 *   dist > KeepAlive                 it stops topping up entirely
 *   an actor further than Despawn    that individual actor is queued for despawn
 *
 * The distance that spawns and the distance that despawns are therefore never the same number, which is
 * what stops a player pacing back and forth over a boundary from producing a spawn/despawn/spawn cycle.
 * The width of that dead band - Despawn minus SpawnIn - is the hysteresis, and it is set in metres of
 * level, not in frames of smoothing.
 */
USTRUCT(BlueprintType)
struct SPAWNBUDGET_API FSpawnBudgetRings
{
	GENERATED_BODY()

	/** At or inside this distance the source fills up to MaxAlive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rings",
		meta = (ClampMin = "0.0", UIMax = "20000.0", ForceUnits = "cm"))
	float SpawnIn = 3000.0f;

	/** Between SpawnIn and this, the source holds what it has. Never smaller than SpawnIn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rings",
		meta = (ClampMin = "0.0", UIMax = "20000.0", ForceUnits = "cm"))
	float KeepAlive = 4500.0f;

	/** An actor further than this from every viewer is queued for despawn. Never smaller than KeepAlive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rings",
		meta = (ClampMin = "0.0", UIMax = "40000.0", ForceUnits = "cm"))
	float Despawn = 6000.0f;

	/** Force SpawnIn <= KeepAlive <= Despawn. Called after any edit, and again at registration. */
	void Sanitise()
	{
		SpawnIn = FMath::Max(0.0f, SpawnIn);
		KeepAlive = FMath::Max(KeepAlive, SpawnIn);
		Despawn = FMath::Max(Despawn, KeepAlive);
	}

	/** The dead band, in centimetres. Zero means a boundary that can flicker. */
	float GetHysteresis() const { return Despawn - SpawnIn; }
};

/** One spawnable class and how much the sort key cares about it. */
USTRUCT(BlueprintType)
struct SPAWNBUDGET_API FSpawnBudgetClassEntry
{
	GENERATED_BODY()

	/** What to spawn. An entry with no class is skipped rather than treated as an error. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget")
	TSubclassOf<AActor> ActorClass;

	/**
	 * How often this class is picked, relative to the other entries on the same source.
	 *
	 * This is the picking weight, not the queue weight - the queue weight lives on the source, because
	 * importance is a property of the place, not of the mesh standing in it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget",
		meta = (ClampMin = "0.0", UIMax = "10.0"))
	float PickWeight = 1.0f;
};

/**
 * What one frame is allowed to cost.
 *
 * Three ceilings rather than one, because the three things that can go wrong are different: too many
 * SpawnActor calls, too many Destroy calls, and one unlucky Blueprint construction script that eats the
 * frame on its own. The count ceilings are predictable and the millisecond ceiling is the backstop.
 */
USTRUCT(BlueprintType)
struct SPAWNBUDGET_API FSpawnBudgetLimits
{
	GENERATED_BODY()

	/** How many actors may be spawned in one frame. The rest of the queue waits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget", meta = (ClampMin = "0", UIMax = "256"))
	int32 MaxSpawnsPerFrame = 4;

	/** How many actors may be despawned in one frame. Usually higher than the spawn budget - a despawn is cheaper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget", meta = (ClampMin = "0", UIMax = "256"))
	int32 MaxDespawnsPerFrame = 8;

	/**
	 * Wall-clock ceiling for servicing the queue, in milliseconds.
	 *
	 * Checked between each item, so one slow actor can overshoot it by exactly one actor and never by
	 * more. Zero means only the count ceilings apply.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Budget",
		meta = (ClampMin = "0.0", UIMax = "16.0", ForceUnits = "ms"))
	float MaxMillisecondsPerFrame = 1.0f;
};

/**
 * Everything the statistics box draws, read once per frame from the running system.
 *
 * Nothing in here is estimated or smoothed. Every field is the number the subsystem actually used on the
 * frame it is read, which is the only reason a box like this is worth putting on screen.
 */
USTRUCT(BlueprintType)
struct SPAWNBUDGET_API FSpawnBudgetStats
{
	GENERATED_BODY()

	/** Actors alive and owned by the plugin right now. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 Population = 0;

	/** The world-wide ceiling the population is measured against. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 MaxPopulation = 0;

	/** Actors spawned during the last serviced frame. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 SpawnsThisFrame = 0;

	/** Actors despawned during the last serviced frame. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 DespawnsThisFrame = 0;

	/** Requests waiting to be spawned. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 SpawnQueueLength = 0;

	/** Actors waiting to be despawned. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 DespawnQueueLength = 0;

	/** The whole tick - evaluate sources, scan live actors, sort, service - in milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	float TickMilliseconds = 0.0f;

	/** Just the part that spawns and despawns, which is the part the millisecond budget guards. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	float ServiceMilliseconds = 0.0f;

	/** True when the last frame stopped on a budget rather than on an empty queue. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	bool bBudgetSaturated = false;

	/** True when the last frame stopped on the millisecond ceiling specifically. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	bool bTimeSliceHit = false;

	/** Spawns served out of a pool since the world started. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 PoolHits = 0;

	/**
	 * Spawns that had to build a brand new actor since the world started.
	 *
	 * This is the number the whole plugin is judged on. With pooling on it climbs while the world fills
	 * and then stands still; with pooling off it climbs forever.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 NewAllocations = 0;

	/** Actors sitting parked in pools, switched off and costing nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 PooledActors = 0;

	/** Requests turned away because the world was already at its population ceiling, or the queue was full. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 Refused = 0;

	/** Sources currently inside their own KeepAlive ring. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 ActiveSources = 0;

	/** Sources registered in this world at all. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	int32 RegisteredSources = 0;

	/** Whether despawned actors are parked in a pool or destroyed. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	bool bPoolingEnabled = true;

	/** The global multiplier currently applied to every source's rings. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	float RingScale = 1.0f;

	/** The budget in force, so the box can print the ceiling next to the number it is comparing against. */
	UPROPERTY(BlueprintReadOnly, Category = "SpawnBudget|Stats")
	FSpawnBudgetLimits Limits;
};

/**
 * One actor parked in a pool, with the list of components that were ticking when it was parked.
 *
 * The list is the part that is easy to get wrong. Switching the actor's own tick off does not stop its
 * components - a movement component, a rotating component, a timeline - so a parked actor keeps costing
 * exactly what it cost while it was visible, and a pool that does that is worse than no pool. Every
 * component that was ticking is switched off and remembered, so that unparking restores the actor to the
 * state it was in rather than to whatever its class defaults happen to say.
 */
USTRUCT()
struct FSpawnBudgetParkedActor
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Actor = nullptr;

	UPROPERTY()
	TArray<TObjectPtr<UActorComponent>> ReEnableTickComponents;
};

/** One actor the plugin currently owns, and what it needs to know to decide when to take it back. */
USTRUCT()
struct FSpawnBudgetLiveActor
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Actor = nullptr;

	/** The source that asked for it, or null for a request that came from Blueprint or from Spawn.Stress. */
	UPROPERTY()
	TWeakObjectPtr<ASpawnSource> Source;

	UPROPERTY()
	TSubclassOf<AActor> ActorClass;

	/**
	 * The rings this actor is measured against, copied at spawn time.
	 *
	 * Copied rather than looked up, so an unowned actor still has rings and a source whose rings are
	 * edited mid-game does not retroactively despawn what it already placed.
	 */
	UPROPERTY()
	FSpawnBudgetRings Rings;

	/** Already in the despawn queue - do not queue it a second time. */
	UPROPERTY()
	bool bDespawnQueued = false;

	/**
	 * Lookup key for the index map, taken while the actor was certainly alive.
	 *
	 * Not the actor pointer. An actor destroyed behind the plugin's back leaves a null TObjectPtr behind,
	 * and a map keyed on that pointer could then never have its entry removed - a slow leak that ends in
	 * a stale index pointing at somebody else's actor. An FObjectKey keeps working after the object it
	 * names has gone, which is exactly when the entry needs removing.
	 */
	FObjectKey Key;
};

/** One request waiting for a spawn budget. */
USTRUCT()
struct FSpawnBudgetPendingSpawn
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<AActor> ActorClass;

	UPROPERTY()
	TWeakObjectPtr<ASpawnSource> Source;

	FTransform Transform = FTransform::Identity;

	UPROPERTY()
	FSpawnBudgetRings Rings;

	/** Importance of the source that asked. Higher means sooner. */
	float Weight = 1.0f;

	/** Distance divided by weight, recomputed when the queue is re-scored. Lower means sooner. */
	float Score = 0.0f;

	/** Was inside the camera cone when it was last scored. */
	bool bVisible = false;

	/** Arrival order, used to break ties so that equal requests stay first-in-first-out. */
	int32 Serial = 0;
};

/** One actor waiting for a despawn budget. */
USTRUCT()
struct FSpawnBudgetPendingDespawn
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> Actor;

	/** Distance to the nearest viewer when it was queued. The furthest goes first. */
	float Score = 0.0f;
};
