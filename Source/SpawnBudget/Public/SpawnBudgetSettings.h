// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/DeveloperSettings.h"
#include "SpawnBudgetTypes.h"
#include "SpawnBudgetSettings.generated.h"

/**
 * Project-wide defaults for SpawnBudget, under Project Settings -> Plugins -> SpawnBudget.
 *
 * Everything here is a default or a ceiling, never a per-place value: what a particular source spawns,
 * where, and how far away it gives up is on the source, because that is what a designer moves around all
 * afternoon. What is here is what a project decides once - what one frame may cost, how large the world
 * is allowed to get, and whether a despawn parks an actor or destroys it.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "SpawnBudget"))
class SPAWNBUDGET_API USpawnBudgetSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USpawnBudgetSettings();

	//~ UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** The settings object, never null. */
	static const USpawnBudgetSettings& Get();

	//~ Master switch ------------------------------------------------------------------------------------

	/**
	 * Run the spawner at all.
	 *
	 * Off leaves the subsystem alive - sources still register, the statistics box still draws, requests
	 * still queue - but nothing is spawned or despawned. That is what a project wants while it is
	 * bisecting a population problem, and it is a far smaller change than pulling the sources out of a map.
	 */
	UPROPERTY(config, EditAnywhere, Category = "General")
	bool bEnabled = true;

	//~ Budget -------------------------------------------------------------------------------------------

	/** What one frame is allowed to cost. Changed at runtime with SetBudget or Spawn.Budget. */
	UPROPERTY(config, EditAnywhere, Category = "Budget")
	FSpawnBudgetLimits Limits;

	/**
	 * The world-wide population ceiling.
	 *
	 * Reached, requests are refused and counted rather than dropped in silence, and lowering it below the
	 * current population queues the excess for despawn, furthest away first. A refusal that shows up on
	 * the statistics box is a design decision; a request that vanishes is a bug report six weeks later.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0", UIMax = "20000"))
	int32 MaxPopulation = 2000;

	/**
	 * How long the queue may get before requests are refused.
	 *
	 * A queue is a promise to spawn something. A queue with no ceiling is a promise a stalled frame budget
	 * can never keep, and it grows until memory notices.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "16", UIMax = "65536"))
	int32 MaxQueueLength = 8192;

	/**
	 * How many live actors are distance-checked per frame. 0 checks all of them.
	 *
	 * The check itself is one squared distance, but it is one per actor, and a world of ten thousand
	 * actors would pay it ten thousand times a frame for no benefit: nothing walks out of a six thousand
	 * centimetre ring in a sixtieth of a second. The scan is a rolling window instead, so its cost is flat
	 * no matter how large the population gets, and a despawn arrives a few frames later than it could have.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "0", UIMax = "8192"))
	int32 MaxLiveScansPerFrame = 1024;

	/**
	 * How often the waiting queue is re-scored against the moved viewer, in seconds. 0 re-scores every frame.
	 *
	 * A score is a distance, and distances go stale as the player moves. Re-scoring every frame is correct
	 * and costs a sort of the whole queue; a quarter of a second is indistinguishable at walking pace and
	 * costs a quarter as much.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget",
		meta = (ClampMin = "0.0", UIMax = "2.0", ForceUnits = "s"))
	float RescoreIntervalSeconds = 0.25f;

	/**
	 * How much a request inside the camera cone is worth against one behind the player.
	 *
	 * 2.0 halves its score and therefore roughly doubles its chance of being next. It is a multiplier on
	 * importance and not a hard rule, because a thing directly behind the player is exactly what they will
	 * see when they turn round.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Budget", meta = (ClampMin = "1.0", UIMax = "8.0"))
	float VisibleWeightMultiplier = 2.0f;

	//~ Pooling ------------------------------------------------------------------------------------------

	/**
	 * Park despawned actors and reuse them, instead of destroying them.
	 *
	 * On is the product. Off exists so the difference can be measured on the statistics box in one click,
	 * and because a project migrating an existing spawner wants to turn the new behaviour on deliberately.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Pooling")
	bool bPoolingEnabled = true;

	/**
	 * How many parked actors one class may hold. Above this, a despawn destroys instead of parking.
	 *
	 * A pool is memory that is not being used for anything. This is where a project decides how much of
	 * that it is willing to trade for never allocating again.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Pooling", meta = (ClampMin = "0", UIMax = "4096"))
	int32 MaxPooledPerClass = 256;

	/**
	 * Move parked actors out of the level rather than leaving them where they died.
	 *
	 * They are hidden and have no collision either way, so this changes nothing that can be seen. It
	 * changes what shows up in a debug capture, what a stray sphere overlap finds, and what an unattached
	 * camera flies past - and it makes an accidentally unhidden pool obvious instead of subtle.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Pooling")
	bool bParkPooledActors = true;

	/** Where parked actors are put when bParkPooledActors is set. */
	UPROPERTY(config, EditAnywhere, Category = "Pooling", meta = (EditCondition = "bParkPooledActors"))
	FVector ParkLocation = FVector(0.0f, 0.0f, -100000.0f);

	//~ Sources ------------------------------------------------------------------------------------------

	/**
	 * Rings used by a request that has no source - a Blueprint RequestSpawn, or Spawn.Stress.
	 *
	 * Such an actor still has to be able to leave, so it is measured against these rather than against
	 * nothing at all. Give them a generous Despawn: an actor a Blueprint asked for by hand was usually
	 * asked for on purpose.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Sources")
	FSpawnBudgetRings DefaultRings;

	/** Channel the ground trace uses when a source projects its scatter points downward. */
	UPROPERTY(config, EditAnywhere, Category = "Sources")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_WorldStatic;

	/**
	 * How many scatter points a source may try before it gives up for this frame.
	 *
	 * A point that falls outside the despawn ring, or off the navmesh, is thrown away and drawn again.
	 * Without a ceiling, a source whose whole area is unreachable would retry forever on every frame; with
	 * one, it quietly places fewer actors, which is what an unreachable area should look like.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Sources", meta = (ClampMin = "1", UIMax = "32"))
	int32 MaxScatterAttempts = 8;

	//~ Presentation -------------------------------------------------------------------------------------

	/** Start with the statistics box on. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bShowStatsByDefault = true;

	/**
	 * Draw the statistics box through AHUD::OnHUDPostRender as well, so a project keeps its own HUD class.
	 *
	 * ASpawnBudgetHUD exists for projects that have no HUD of their own. This is for the far more common
	 * case where a project already has one and is not about to reparent it to see a spawner's numbers.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bAutoDrawStatsOnAnyHUD = false;

	/** Start with the ring drawing on. Equivalent to Spawn.Debug 1 at startup, and compiled out in Shipping. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bDebugDrawByDefault = false;
};
