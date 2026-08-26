# SpawnBudget — Documentation

**Budgeted world spawner with distance rings and actor pooling.**
Unreal Engine **5.8** · Plugin version **1.0.0** · One runtime module, no editor module.

---

## Table of contents

1. [What this plugin does](#1-what-this-plugin-does)
2. [Installation](#2-installation)
3. [Quick start (5 minutes)](#3-quick-start-5-minutes)
4. [How it works](#4-how-it-works)
5. [Class and API overview](#5-class-and-api-overview)
6. [Code examples](#6-code-examples)
7. [Project settings reference](#7-project-settings-reference)
8. [Console commands](#8-console-commands)
9. [Reading the statistics box](#9-reading-the-statistics-box)
10. [Supported platforms and engine versions](#10-supported-platforms-and-engine-versions)
11. [Limitations and known behaviour](#11-limitations-and-known-behaviour)
12. [Troubleshooting](#12-troubleshooting)
13. [Support](#13-support)

---

## 1. What this plugin does

A world spawner that cannot spike the frame.

Instead of "spawn everything that is due", SpawnBudget spawns **at most N actors and despawns at most
M actors per frame**, and never spends more than a set number of **milliseconds** doing it. Whatever
is left over waits in a queue sorted by importance and continues on the next frame from exactly where
it stopped. Nothing is lost and nothing is spawned twice.

Three things make up the product:

| | |
|---|---|
| **A budget per frame** | `MaxSpawnsPerFrame`, `MaxDespawnsPerFrame`, `MaxMillisecondsPerFrame`. The queue drains at a rate you set, not at the rate the level happens to ask for. |
| **Three distance rings** | `SpawnIn` fills a source, `KeepAlive` holds it, `Despawn` takes actors back. The gap between the first and the last is deliberate hysteresis — a player pacing over a boundary produces no flicker. |
| **A per-class pool** | A despawn hides the actor, takes it and its components off the tick, removes collision and parks it. A spawn takes it back out and resets it. The garbage collector never hears about it. |

And one number that proves it: **Pool hits against New allocations**, drawn on screen. In the steady
state the second number stands still. That is the claim, and it is on the HUD rather than in a
sentence.

---

## 2. Installation

### From Fab / a downloaded archive

1. Close the Unreal Editor.
2. Copy the `SpawnBudget` folder into your project's `Plugins` directory:
   ```
   <YourProject>/Plugins/SpawnBudget/
   ```
   (Create the `Plugins` folder if your project does not have one yet.)
3. **C++ projects:** right-click `<YourProject>.uproject` → *Generate Visual Studio project files*,
   then build the project. The plugin builds with it.
   **Blueprint-only projects:** open the project; the editor will offer to build the missing module.
   Accept. (A Blueprint-only project needs a C++ toolchain installed once — Visual Studio 2022 with
   the "Game development with C++" workload on Windows, Xcode on macOS.)
4. Start the editor. Go to **Edit → Plugins → Gameplay → SpawnBudget** and confirm it is enabled.
   Restart if the editor asks you to.

### Engine-wide installation (optional)

Copy the folder to `<EngineRoot>/Engine/Plugins/Marketplace/SpawnBudget/` instead if you want it
available to every project on the machine.

### Verifying the install

Open **Project Settings → Plugins → SpawnBudget**. If that section exists, the module loaded.
In Play-In-Editor, the console command `Spawn.Stats` prints the counters to the log.

---

## 3. Quick start (5 minutes)

### Step 1 — Place a spawn source

In the **Place Actors** panel search for **Spawn Source** and drag one into your level.

### Step 2 — Tell it what to spawn

Select it and, in the Details panel under **SpawnBudget | What**:

* **Classes** — add one entry, set `Actor Class` to whatever you want spawned (an enemy, a prop, a
  Blueprint actor). Add more entries for a mix; `Pick Weight` decides how often each is chosen
  relative to the others on the same source.
* **Max Alive** — how many of them should exist while the player is inside the source's `SpawnIn`
  ring. Default `24`.
* **Weight** — how important this source's requests are against every other source competing for the
  same frame budget. Leave scenery at `1.0`; give the ambush `2.0`.

### Step 3 — Tell it where and when

Under **SpawnBudget | Where**:

* **Scatter** — `Point`, `Circle` (default), `Box` or `Spline`.
* **Scatter Radius** — for `Circle`, the disc radius; for `Spline`, the sideways offset. Default `1500` cm.
* **Ground Mode** — `Line Trace` (default) drops each point onto whatever is actually under it.
  `Nav Mesh` refuses points nothing could walk on. `None` leaves them where they are (flying things).
* **Spawn Height Offset** — lift each point so a capsule does not start inside the floor.

Under **SpawnBudget | When → Rings**:

* **Spawn In** `3000` — inside this the source fills up to `MaxAlive`.
* **Keep Alive** `4500` — between the two it holds what it has.
* **Despawn** `6000` — past this, an individual actor is queued for despawn.

### Step 4 — See the numbers

Set the level's **HUD Class** to `Spawn Budget HUD` (World Settings → Game Mode, or your GameMode
Blueprint). If your project already has its own HUD class you do not have to replace it — instead
tick **Project Settings → Plugins → SpawnBudget → Presentation → Auto Draw Stats On Any HUD**, and
the same box is drawn through `AHUD::OnHUDPostRender`. The two paths know about each other and
cannot stack.

### Step 5 — Play

Press Play. Walk towards the source and it fills at four actors per frame. Walk away and its actors
are taken back one ring later. Watch **Pool hits** climb while **New allocations** stops moving.

That is the whole setup: no Blueprint, no trigger, no timer.

---

## 4. How it works

### One tick, one queue

`ASpawnSource` **does not tick** and never calls `SpawnActor`. The world subsystem walks every source
once per frame, decides what should exist, and puts requests into one queue that one budget drains.
That is why a hundred sources cost roughly what one costs, and why a level that suddenly needs four
hundred actors does not stall on the frame it needs them.

The per-frame tick has five stages:

1. **Update viewers** — one entry per local player camera (location, forward vector, half-FOV cosine).
2. **Evaluate sources** — for each source: distance to the nearest viewer, ring test, and up to
   `MaxRequestsPerEvaluation` new requests if it is short of `MaxAlive`.
3. **Scan live actors** — a rolling window of up to `MaxLiveScansPerFrame` actors is distance-tested;
   anything past its `Despawn` ring is queued. The window rolls, so the cost stays flat no matter how
   large the population gets.
4. **Re-score the queue** — every `RescoreIntervalSeconds` the waiting requests are re-scored against
   the moved viewer and re-sorted.
5. **Service the queues** — spend the budget, then stop.

### The sort key

```
Score = DistanceToNearestViewer / SourceWeight
Score = Score / VisibleWeightMultiplier   if the point is inside a camera cone
```

Lower is sooner. Ties are broken by arrival order, so equal requests stay first-in-first-out. Despawns
are sorted the other way round: the furthest actor goes first.

### Hysteresis, concretely

```
dist <= SpawnIn                the source fills itself up to MaxAlive
SpawnIn < dist <= KeepAlive    it holds — nothing added, nothing taken away
dist > KeepAlive               it stops topping up
an actor further than Despawn  that individual actor is queued for despawn
```

The distance that spawns and the distance that despawns are therefore never the same number. The dead
band is `Despawn - SpawnIn`, measured in centimetres of level rather than in frames of smoothing —
which means it can be reasoned about on a map.

### The millisecond ceiling

`MaxMillisecondsPerFrame` is checked **between** items, so one slow actor can overshoot it by exactly
one actor and never by more. Set it to `0` to let only the count ceilings apply.

### What a despawn actually does

`USpawnBudgetPool::Release` hides the actor, disables collision, switches off the actor's tick **and
the tick of every component that was running** — remembering which ones, so unparking restores the
state it was in rather than whatever its class defaults say — and optionally teleports it to
`ParkLocation` (default `(0, 0, -100000)`).

Switching off the actor's own tick does not stop its components. A movement component, a rotating
component or a timeline would keep costing exactly what it cost while visible, and a pool that does
that is worse than no pool. Hence the component list.

If the pool for that class is already at `MaxPooledPerClass`, `Release` returns false and the
subsystem destroys the actor instead.

### Requests that are refused

`RequestSpawn` returns `false` when the request was **refused** rather than queued. That happens for
exactly two reasons, and both are counted in the `Refused` statistic:

* the world is already at `MaxPopulation`, or
* the queue is already at `MaxQueueLength`.

A queued request is a promise. Refusing one out loud is honest; dropping one silently is the bug that
gets found six weeks later in a bug report about an empty village.

---

## 5. Class and API overview

| Class | Kind | Purpose |
|---|---|---|
| `ASpawnSource` | `AActor`, Blueprintable | A place in the level that wants a population. Classes, weights, rings, scatter shape, ground projection. |
| `USpawnBudgetSubsystem` | `UTickableWorldSubsystem` | The whole plugin: queue, budget, pools, statistics. Game and PIE worlds only. |
| `USpawnBudgetPool` | `UObject` | Parked actors of exactly one class, plus `PoolHits` / `NewAllocations`. |
| `ISpawnBudgetPoolable` | `UInterface`, Blueprintable | Two events for actors with state a pool cannot reset on its own. |
| `USpawnBudgetStatics` | `UBlueprintFunctionLibrary` | The whole plugin from Blueprint, without a subsystem reference in sight. |
| `USpawnBudgetSettings` | `UDeveloperSettings` | Project-wide defaults under *Project Settings → Plugins → SpawnBudget*. |
| `ASpawnBudgetHUD` | `AHUD`, Blueprintable | The statistics box, drawn on `UCanvas`. Survives a cooked Shipping build. |

### Structs and enums

| Type | Fields |
|---|---|
| `FSpawnBudgetRings` | `SpawnIn`, `KeepAlive`, `Despawn` (cm). `Sanitise()` forces `SpawnIn <= KeepAlive <= Despawn`; `GetHysteresis()` returns the dead band. |
| `FSpawnBudgetLimits` | `MaxSpawnsPerFrame` (4), `MaxDespawnsPerFrame` (8), `MaxMillisecondsPerFrame` (1.0). |
| `FSpawnBudgetClassEntry` | `ActorClass`, `PickWeight`. |
| `FSpawnBudgetStats` | Everything the statistics box draws — see [§9](#9-reading-the-statistics-box). |
| `ESpawnScatterShape` | `Point`, `Circle`, `Box`, `Spline`. |
| `ESpawnGroundMode` | `None`, `LineTrace`, `NavMesh`. |

### `ASpawnSource`

**Properties — What**

| Property | Type | Default | Meaning |
|---|---|---|---|
| `Classes` | `TArray<FSpawnBudgetClassEntry>` | empty | What to spawn, with pick weights. An empty list is not an error. |
| `MaxAlive` | `int32` | `24` | How many actors this source wants alive inside its `SpawnIn` ring. |
| `Weight` | `float` | `1.0` | Queue importance. `2.0` behaves as though it were half as far away. |
| `bSourceEnabled` | `bool` | `true` | Off stops it asking. It does **not** take away what it already placed. |

**Properties — Where**

| Property | Type | Default | Meaning |
|---|---|---|---|
| `Scatter` | `ESpawnScatterShape` | `Circle` | How points are spread. |
| `ScatterRadius` | `float` | `1500` cm | Disc radius (`Circle`) / sideways offset (`Spline`). |
| `ScatterExtent` | `FVector` | `(1500,1500,0)` | Half-size for `Box`, in the source's own space. |
| `GroundMode` | `ESpawnGroundMode` | `LineTrace` | What a point is snapped to. |
| `GroundTraceHeight` | `float` | `2000` cm | How far up the trace starts and how far down it goes. |
| `NavProjectionExtent` | `float` | `300` cm | How far off a navmesh a point may be and still be pulled onto it. |
| `SpawnHeightOffset` | `float` | `0` cm | Lift after snapping, so a capsule does not start inside the floor. |
| `bRandomYaw` | `bool` | `true` | Random yaw per spawn. Off keeps the source's rotation (a line of soldiers). |

**Properties — When**

| Property | Type | Default | Meaning |
|---|---|---|---|
| `Rings` | `FSpawnBudgetRings` | `3000 / 4500 / 6000` | The three distances. |
| `RefillIntervalSeconds` | `float` | `0` | Shortest gap between two batches from this source. A drip feed, not a budget. |
| `MaxRequestsPerEvaluation` | `int32` | `16` | So one source cannot fill the whole queue in one frame. |

**Functions**

```cpp
int32                GetLiveCount() const;            // alive from this source right now
int32                GetPendingCount() const;         // still waiting in the queue
float                GetLastViewerDistance() const;   // -1 before the first evaluation
bool                 IsInRange() const;               // inside its own KeepAlive ring
void                 SetSourceEnabled(bool bNewEnabled);
void                 DespawnAll();                    // queue everything it placed
TSubclassOf<AActor>  PickClass() const;               // weighted pick, null when nothing to pick
```

`ScatterSpline` is a `USplineComponent` on every source, empty by default. Add points to it to use
`ESpawnScatterShape::Spline`.

### `USpawnBudgetSubsystem`

```cpp
static USpawnBudgetSubsystem* Get(const UObject* WorldContextObject);

// Requests
bool  RequestSpawn(TSubclassOf<AActor> ActorClass, const FTransform& Transform, float Weight = 1.0f);
void  ReleaseActor(AActor* Actor);
int32 RequestStress(int32 Count);
void  FlushQueue();      // throw the waiting queue away; nothing alive is touched
void  ClearAll();        // despawn everything, empty both queues, destroy every parked actor

// Budget
void  SetBudget(int32 MaxSpawnsPerFrame, int32 MaxDespawnsPerFrame, float MaxMillisecondsPerFrame);
FSpawnBudgetLimits GetBudget() const;
void  SetMaxPopulation(int32 NewMaxPopulation);
int32 GetMaxPopulation() const;
int32 GetPopulation() const;
int32 GetQueueLength() const;          // spawns + despawns
int32 GetSpawnQueueLength() const;
int32 GetDespawnQueueLength() const;

// Switches
void  SetPoolingEnabled(bool bNewEnabled);
bool  IsPoolingEnabled() const;
void  SetRingScale(float NewRingScale);   // multiply every source's rings at once
float GetRingScale() const;
void  SetEnabled(bool bNewEnabled);
bool  IsEnabled() const;
void  SetDebugDraw(bool bNewEnabled);     // compiled out of a Shipping build

// Sources
void  GetSources(TArray<ASpawnSource*>& OutSources) const;
void  DespawnSource(ASpawnSource* Source);

// Statistics
FSpawnBudgetStats GetStats() const;
int32 GetPoolHits() const;
int32 GetNewAllocations() const;
int32 GetRefusedCount() const;

// Events
FSpawnBudgetSpawned   OnActorSpawned;    // (AActor* Actor, ASpawnSource* Source)
FSpawnBudgetDespawned OnActorDespawned;  // fired while the actor is still valid
```

`SetBudget` treats a **negative value as "leave that ceiling alone"**.

`ReleaseActor` is safe for an actor the plugin never spawned — it is simply not ours, nothing happens,
and nothing is logged. A game calling it from a shared death handler is doing the right thing.

`DoesSupportWorldType` accepts **Game and PIE only**. An editor viewport is the level designer's;
filling it with wandering actors while somebody is building the level is help nobody asked for.

### `USpawnBudgetStatics` (Blueprint)

Every node is world-context-aware and safe in a world that has no SpawnBudget subsystem at all:
queries answer nothing, setters do nothing, nobody crashes. A Blueprint written against SpawnBudget
still runs in a test map where no source was ever placed.

| Node | Notes |
|---|---|
| `Request Spawn` | Returns false when refused. |
| `Release Actor` | Safe for foreign actors. |
| `Request Stress` | Default `1000`. Returns how many were accepted. |
| `Flush Queue` / `Clear All` | |
| `Set Budget` | `MaxDespawnsPerFrame` and `MaxMillisecondsPerFrame` default to `-1` = unchanged. |
| `Get Budget` | |
| `Set Max Population` / `Get Max Population` | |
| `Get Population` | |
| `Get Queue Length` / `Get Spawn Queue Length` / `Get Despawn Queue Length` | |
| `Set Pooling Enabled` / `Is Pooling Enabled` | |
| `Set Ring Scale` / `Get Ring Scale` | |
| `Set Spawning Enabled` / `Is Spawning Enabled` | |
| `Set Debug Draw` | |
| `Get Stats` / `Get Pool Hits` / `Get New Allocations` / `Get Refused Count` | |
| `Get Sources` / `Despawn Source` | |

### `ISpawnBudgetPoolable`

The pool already restores everything the engine knows about: transform, visibility, collision, the
actor's tick and the tick of every component that was running. What it cannot know is what the actor
*means*. That is what these two `BlueprintNativeEvent`s are for:

```cpp
void OnSpawnedFromPool(ASpawnSource* Source, bool bWasPooled);
void OnReturnedToPool();
```

`OnSpawnedFromPool` is called for a **brand new actor as well as a reused one**, so there is exactly
one code path to get right rather than two that have to agree. `bWasPooled` tells you which case you
are in.

An actor that implements nothing still pools correctly — it simply comes back exactly as it went
away. That is right for a rock and wrong for an enemy, and the plugin cannot tell the difference, so
it asks.

---

## 6. Code examples

### C++ — asking for an actor by hand

```cpp
#include "SpawnBudgetSubsystem.h"

void AMyGameMode::SpawnReinforcement(const FVector& Where)
{
    if (USpawnBudgetSubsystem* Spawner = USpawnBudgetSubsystem::Get(this))
    {
        const FTransform Transform(FRotator::ZeroRotator, Where);

        // Weight 3.0 — reinforcements matter more than ambient life competing for the same budget.
        const bool bQueued = Spawner->RequestSpawn(AEnemyGrunt::StaticClass(), Transform, 3.0f);

        if (!bQueued)
        {
            // Refused: population ceiling or queue ceiling. Not a silent drop.
            UE_LOG(LogTemp, Warning, TEXT("Reinforcement refused — population %d/%d."),
                Spawner->GetPopulation(), Spawner->GetMaxPopulation());
        }
    }
}
```

The actor does **not** exist when `RequestSpawn` returns — it exists when the budget can afford it.
Bind to `OnActorSpawned` if something has to happen at that moment.

### C++ — handing an actor back instead of destroying it

```cpp
void AEnemyGrunt::Die()
{
    PlayDeathEffects();

    if (USpawnBudgetSubsystem* Spawner = USpawnBudgetSubsystem::Get(this))
    {
        Spawner->ReleaseActor(this);   // parked and reused, not destroyed
    }
    else
    {
        Destroy();                     // no plugin in this world — plain fallback
    }
}
```

### C++ — an actor that resets itself for the pool

```cpp
// EnemyGrunt.h
#include "SpawnBudgetPoolable.h"

UCLASS()
class AEnemyGrunt : public ACharacter, public ISpawnBudgetPoolable
{
    GENERATED_BODY()

public:
    virtual void OnSpawnedFromPool_Implementation(ASpawnSource* Source, bool bWasPooled) override;
    virtual void OnReturnedToPool_Implementation() override;

private:
    UPROPERTY() float Health = 100.0f;
};

// EnemyGrunt.cpp
void AEnemyGrunt::OnSpawnedFromPool_Implementation(ASpawnSource* Source, bool bWasPooled)
{
    // Runs for a new actor and a reused one alike — one path, not two that have to agree.
    Health = 100.0f;
    Ammo   = MaxAmmo;

    if (AAIController* AI = Cast<AAIController>(GetController()))
    {
        AI->GetBrainComponent()->RestartLogic();
    }
}

void AEnemyGrunt::OnReturnedToPool_Implementation()
{
    // Stop what the engine will not stop for you.
    GetWorldTimerManager().ClearAllTimersForObject(this);
    StopAllAudio();
}
```

Do **not** call `Destroy()` in `OnReturnedToPool` — the pool is about to take ownership of the actor.

### C++ — reacting to spawns and despawns

```cpp
void AMyGameMode::BeginPlay()
{
    Super::BeginPlay();

    if (USpawnBudgetSubsystem* Spawner = USpawnBudgetSubsystem::Get(this))
    {
        Spawner->OnActorSpawned.AddDynamic(this, &AMyGameMode::HandleSpawned);
        Spawner->OnActorDespawned.AddDynamic(this, &AMyGameMode::HandleDespawned);
    }
}

void AMyGameMode::HandleSpawned(AActor* Actor, ASpawnSource* Source)
{
    if (ATeamMember* Member = Cast<ATeamMember>(Actor))
    {
        Member->SetTeam(Source ? Source->GetTeamTag() : DefaultTeam);
    }
}
```

`OnActorDespawned` fires while the actor is **still valid**, immediately before it is parked or
destroyed.

### C++ — a quality setting that pulls the whole world in

```cpp
void UMyGameUserSettings::ApplyPopulationScalability(int32 Level)
{
    USpawnBudgetSubsystem* Spawner = USpawnBudgetSubsystem::Get(GetWorld());
    if (!Spawner) { return; }

    switch (Level)
    {
    case 0:  // low-end
        Spawner->SetBudget(2, 8, 0.5f);
        Spawner->SetMaxPopulation(400);
        Spawner->SetRingScale(0.6f);
        break;

    case 1:  // default
        Spawner->SetBudget(4, 8, 1.0f);
        Spawner->SetMaxPopulation(2000);
        Spawner->SetRingScale(1.0f);
        break;

    case 2:  // high-end
        Spawner->SetBudget(16, 32, 3.0f);
        Spawner->SetMaxPopulation(5000);
        Spawner->SetRingScale(1.5f);
        break;
    }
}
```

`SetRingScale` multiplies every source's rings at once without touching a single source — so a
platform profile does not require a designer to have exposed a scalar on every actor in the level.

### C++ — reading the numbers into your own telemetry

```cpp
void AMyPerfLogger::SampleSpawner()
{
    const USpawnBudgetSubsystem* Spawner = USpawnBudgetSubsystem::Get(this);
    if (!Spawner) { return; }

    const FSpawnBudgetStats Stats = Spawner->GetStats();

    // The number the plugin is judged on: in the steady state this must stop moving.
    CSV_CUSTOM_STAT(Spawner, NewAllocations, Stats.NewAllocations, ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(Spawner, PoolHits,       Stats.PoolHits,       ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(Spawner, ServiceMs,      Stats.ServiceMilliseconds, ECsvCustomStatOp::Set);

    if (Stats.bTimeSliceHit)
    {
        UE_LOG(LogTemp, Verbose, TEXT("Queue stopped on the millisecond ceiling: %d still waiting."),
            Stats.SpawnQueueLength);
    }
}
```

### C++ — creating a source at runtime

```cpp
ASpawnSource* AMyDirector::CreateAmbushSource(const FVector& Where)
{
    ASpawnSource* Source = GetWorld()->SpawnActor<ASpawnSource>(Where, FRotator::ZeroRotator);
    if (!Source) { return nullptr; }

    FSpawnBudgetClassEntry Entry;
    Entry.ActorClass = AEnemyGrunt::StaticClass();
    Entry.PickWeight = 1.0f;
    Source->Classes.Add(Entry);

    Source->MaxAlive          = 12;
    Source->Weight            = 4.0f;                       // an ambush jumps the queue
    Source->Scatter           = ESpawnScatterShape::Circle;
    Source->ScatterRadius     = 800.0f;
    Source->GroundMode        = ESpawnGroundMode::NavMesh;   // nothing spawns where nothing can walk
    Source->Rings.SpawnIn     = 2500.0f;
    Source->Rings.KeepAlive   = 3500.0f;
    Source->Rings.Despawn     = 5000.0f;
    Source->Rings.Sanitise();

    return Source;   // it registers itself on BeginPlay
}
```

### Blueprint

**Request a spawn**

```
Event Something Happened
  → SpawnBudget · Request Spawn
       Actor Class : BP_Enemy
       Transform   : Make Transform (Location = Hit Location)
       Weight      : 2.0
    ↳ Return Value (bool) — false means refused, not queued
```

**A quality button**

```
On Clicked (Low)
  → SpawnBudget · Set Budget        (Max Spawns Per Frame = 2, others left at -1)
  → SpawnBudget · Set Max Population (400)
  → SpawnBudget · Set Ring Scale     (0.6)
```

**A pooled Blueprint actor**

1. Open your actor Blueprint → **Class Settings → Interfaces → Add → Spawn Budget Poolable**.
2. In **My Blueprint → Interfaces**, double-click `On Spawned From Pool` and `On Returned To Pool`.
3. In `On Spawned From Pool`: set `Health = MaxHealth`, reset your state machine, restart the AI.
   The `Was Pooled` pin tells you whether this actor was reused or just constructed.
4. In `On Returned To Pool`: clear timers, stop sounds, stop latent actions. Do not destroy.

**Show the stats box from your own HUD** — tick *Auto Draw Stats On Any HUD* in Project Settings, or
reparent your HUD Blueprint to `Spawn Budget HUD` and call `Toggle Stats` from a button.

---

## 7. Project settings reference

**Project Settings → Plugins → SpawnBudget.** Everything here is a default or a ceiling, never a
per-place value — what a particular source spawns, where, and how far away it gives up belongs on the
source, because that is what a designer moves around all afternoon.

### General

| Setting | Default | Meaning |
|---|---|---|
| `bEnabled` | `true` | Run the spawner at all. Off leaves the subsystem alive — sources still register, the box still draws, requests still queue — but nothing is spawned or despawned. Useful while bisecting a population problem. |

### Budget

| Setting | Default | Meaning |
|---|---|---|
| `Limits.MaxSpawnsPerFrame` | `4` | Actors that may be spawned in one frame. |
| `Limits.MaxDespawnsPerFrame` | `8` | Actors that may be despawned in one frame. Usually higher — a despawn is cheaper. |
| `Limits.MaxMillisecondsPerFrame` | `1.0` | Wall-clock ceiling for servicing the queue. `0` = only the count ceilings apply. |
| `MaxPopulation` | `2000` | World-wide ceiling. Lowering it below the current population queues the excess, furthest away first. |
| `MaxQueueLength` | `8192` | How long the queue may get before requests are refused. A queue with no ceiling is a promise a stalled budget can never keep. |
| `MaxLiveScansPerFrame` | `1024` | Live actors distance-checked per frame; `0` checks all. The rolling window keeps the cost flat at any population. |
| `RescoreIntervalSeconds` | `0.25` | How often the waiting queue is re-scored against the moved viewer. `0` = every frame. A quarter of a second is indistinguishable at walking pace and costs a quarter as much. |
| `VisibleWeightMultiplier` | `2.0` | What a request inside the camera cone is worth against one behind the player. A multiplier on importance, not a hard rule — what is behind the player is exactly what they see when they turn round. |

### Pooling

| Setting | Default | Meaning |
|---|---|---|
| `bPoolingEnabled` | `true` | Park despawned actors and reuse them, instead of destroying them. **On is the product.** Off exists so the difference can be measured on the box in one click. |
| `MaxPooledPerClass` | `256` | How many parked actors one class may hold. Above this a despawn destroys instead of parking. A pool is memory not being used for anything — this is where you decide how much of it to trade for never allocating again. |
| `bParkPooledActors` | `true` | Move parked actors out of the level rather than leaving them where they died. They are hidden and non-colliding either way; this changes what a debug capture, a stray sphere overlap or an unattached camera finds — and makes an accidentally unhidden pool obvious instead of subtle. |
| `ParkLocation` | `(0, 0, -100000)` | Where parked actors are put. |

### Sources

| Setting | Default | Meaning |
|---|---|---|
| `DefaultRings` | `6000 / 9000 / 12000` | Rings for a request with no source (a Blueprint `RequestSpawn`, or `Spawn.Stress`). Deliberately wider than a source's own defaults: an actor asked for by name was asked for on purpose. |
| `GroundTraceChannel` | `ECC_WorldStatic` | Channel the downward scatter trace uses. |
| `MaxScatterAttempts` | `8` | How many scatter points a source may try before giving up this frame. Without a ceiling, a source whose whole area is unreachable would retry forever; with one, it quietly places fewer actors — which is what an unreachable area should look like. |

### Presentation

| Setting | Default | Meaning |
|---|---|---|
| `bShowStatsByDefault` | `true` | Start with the statistics box on. |
| `bAutoDrawStatsOnAnyHUD` | `false` | Draw the box through `AHUD::OnHUDPostRender` too, so a project keeps its own HUD class. |
| `bDebugDrawByDefault` | `false` | Start with ring drawing on. Equivalent to `Spawn.Debug 1` at startup; compiled out in Shipping. |

---

## 8. Console commands

| Command | Effect |
|---|---|
| `Spawn.Stats` | Print the measured counters to the log. |
| `Spawn.Budget <Spawns> [Despawns] [Milliseconds]` | Set what one frame may cost. No arguments prints the current budget. |
| `Spawn.Population <Max>` | Set the world population ceiling. Lowering it despawns the excess, furthest first. |
| `Spawn.Clear` | Despawn everything, empty both queues and destroy every parked actor. |
| `Spawn.Stress <Count>` | Throw `Count` extra requests at the queue, spread over every source. |
| `Spawn.Pool [0\|1]` | Park despawned actors and reuse them, or destroy them. No argument toggles. |
| `Spawn.Rings <Scale>` | Multiply every source's rings by `Scale`, without touching a single source. |
| `Spawn.Sources` | Print every source with its distance, live count and rings. |
| `Spawn.Debug [0\|1]` | Draw every source's three rings and the queued spawn points. |

The two commands that demonstrate the product in ten seconds:

```
Spawn.Budget 4          Spawn.Stress 1000     → queue grows, ms/tick stays flat
Spawn.Budget 64         Spawn.Stress 1000     → same job, drained in seconds
Spawn.Pool 0            (watch New allocations climb)
Spawn.Pool 1            (watch it stand still while Pool hits runs)
```

---

## 9. Reading the statistics box

Nothing in `FSpawnBudgetStats` is estimated or smoothed. Every field is the number the subsystem
actually used on the frame it is read — which is the only reason a box like this is worth putting on
screen.

| Field | Meaning |
|---|---|
| `Population` / `MaxPopulation` | Actors alive and owned by the plugin, against the world ceiling. |
| `SpawnsThisFrame` / `DespawnsThisFrame` | What the last serviced frame actually did. Compare against your budget. |
| `SpawnQueueLength` / `DespawnQueueLength` | What is still waiting. |
| `TickMilliseconds` | The whole tick — evaluate, scan, sort, service. |
| `ServiceMilliseconds` | Just the spawning and despawning, which is the part the millisecond budget guards. |
| `bBudgetSaturated` | The last frame stopped on a budget rather than on an empty queue. |
| `bTimeSliceHit` | It stopped on the millisecond ceiling specifically. |
| **`PoolHits`** | Spawns served out of a pool since the world started. |
| **`NewAllocations`** | Spawns that had to build a brand new actor. **The number the plugin is judged on** — with pooling on it climbs while the world fills and then stands still; with pooling off it climbs forever. |
| `PooledActors` | Actors parked in pools, switched off, costing nothing. |
| `Refused` | Requests turned away by the population ceiling or the queue ceiling. |
| `ActiveSources` / `RegisteredSources` | Sources inside their own `KeepAlive` ring, against sources registered at all. |
| `bPoolingEnabled`, `RingScale`, `Limits` | The configuration in force, printed next to the numbers it is being compared against. |

The box is drawn on `UCanvas` from `AHUD` rather than in UMG on purpose: it has to survive a **cooked
Shipping build**. `DrawDebug` is compiled out there and a debug widget is usually stripped; a Canvas
overlay is not. For a plugin whose entire claim is a number, a number that disappears in the build
that ships is a number nobody can check.

There are deliberately **no buttons on the box**. An `AHUD` hit box is tested against
`UGameViewportClient::GetMousePosition()`, which reports nothing on a machine with no mouse attached —
a capture rig, a build agent, a headless test — so the click never lands. Anything that has to be
clicked belongs in a UMG widget calling `USpawnBudgetStatics`.

---

## 10. Supported platforms and engine versions

### Engine

| | |
|---|---|
| **Unreal Engine** | **5.8** (`EngineVersion: 5.8.0`) |
| Earlier versions | Not supported and not tested. |

### Platforms

The plugin descriptor declares `PlatformAllowList: ["Win64"]`.

| Platform | Status |
|---|---|
| **Win64** | **Built and verified.** |
| **Mac** | Not listed in the descriptor, **not built for this release**. |
| **Linux** | Not listed in the descriptor, **not built for this release**. |

Nothing in the code is platform-specific — the module depends only on `Core`, `CoreUObject`, `Engine`,
`DeveloperSettings` and `RenderCore`, and uses no platform APIs, no plugin dependencies and no
third-party libraries. Mac and Linux are expected to build; they are listed as unverified because
they were not built, not because a problem is known.

### Module layout

| | |
|---|---|
| Modules | One: `SpawnBudget` |
| Type | `Runtime` |
| Loading phase | `PreDefault` |
| Public dependencies | `Core`, `CoreUObject`, `Engine`, `DeveloperSettings` |
| Private dependencies | `RenderCore` (for `GWhiteTexture`, the one-pixel texture the stats box is tiled from) |

**Deliberately not dependencies:** UMG (the box is Canvas so it survives Shipping), Niagara (nothing
here is a particle), Chaos (the only physics is one optional downward line trace, a plain world
query), NavigationSystem (the optional navmesh projection goes through `INavigationDataInterface`,
which lives in `Engine` — a project without a navmesh loses the projection and keeps everything else),
and `UnrealEd` (there is no editor module, so nothing can go missing between what a designer places
and what the packaged game runs).

### Build configurations

Development, DebugGame, Test and Shipping. In Shipping, `Spawn.Debug` ring drawing is compiled out
(`ENABLE_DRAW_DEBUG`); the statistics box and every console command still work.

### Networking

The plugin is **single-world and authority-agnostic**: the subsystem runs in whichever world it is
created in, and it spawns through the normal `UWorld::SpawnActor` path, so actors marked as
replicated replicate as they normally would. There is no built-in client/server population
negotiation. On a dedicated server there are no local player cameras, so viewers must come from your
own logic if you intend to drive server-side population by player position.

---

## 11. Limitations and known behaviour

* **Game and PIE worlds only.** `DoesSupportWorldType` rejects editor worlds by design; you will not
  see spawning in a non-playing viewport.
* **A request is not a guarantee.** `RequestSpawn` returning `true` means *queued*, not *spawned*.
  The actor exists when the budget can afford it. Bind `OnActorSpawned` if you need that moment.
* **Sources ask, they never spawn.** A source getting what it asked for is subject to the frame
  budget, the population ceiling, and the queue ahead of it.
* **The live scan is a rolling window.** With `MaxLiveScansPerFrame` below your population, a despawn
  arrives a few frames later than it theoretically could. That is the trade that keeps the cost flat.
* **Pool ceiling overflow destroys.** Past `MaxPooledPerClass`, a despawn destroys instead of
  parking, and `NewAllocations` will move again. If it does not stand still in a steady state, either
  the pool ceiling is too low for the population or something is destroying actors behind the
  plugin's back.
* **Pooled actors keep their identity.** An actor that is not `ISpawnBudgetPoolable` comes back
  exactly as it went away. That is correct for a rock and wrong for an enemy — implement the
  interface for anything with state.
* **`NavMesh` ground mode falls back to `LineTrace`** when there is no navigation data in the level.
* **No editor module.** There is no custom detail panel, no viewport ring gizmo in the editor, and no
  editor-time preview of a population. Use `Spawn.Debug 1` in PIE.
* **Debug ring drawing is compiled out of Shipping.** The statistics box is not.

---

## 12. Troubleshooting

**Nothing spawns.**
Check, in order: the source has at least one `Classes` entry with an `Actor Class` set; `MaxAlive > 0`;
`bSourceEnabled` is on; you are inside the `SpawnIn` ring (run `Spawn.Sources` to print each source's
distance); `bEnabled` in Project Settings is on; the world is Game or PIE, not an editor viewport.

**Actors spawn inside the floor, or fall through it.**
Set `SpawnHeightOffset` to roughly the capsule half-height. Check `GroundTraceChannel` matches your
floor's collision, and that `GroundTraceHeight` reaches it.

**Actors spawn and vanish immediately.**
The scatter area is reaching past the `Despawn` ring. Either shrink `ScatterRadius` / `ScatterExtent`
or widen `Rings.Despawn`. (Points that would land outside the despawn ring are rejected and redrawn,
but a source whose whole area is outside it will simply place fewer actors.)

**Flickering at a ring boundary.**
`Despawn - SpawnIn` is too small. Widen the gap — that dead band *is* the anti-flicker mechanism.

**`New allocations` keeps climbing.**
Pooling is off (`Spawn.Pool 1`), or `MaxPooledPerClass` is below the number of that class the world
holds, or your own code is calling `Destroy()` on actors the plugin owns instead of
`ReleaseActor()`.

**Enemies come back dead / with no ammo / stuck in their old state.**
They are pooled and not resetting themselves. Implement `ISpawnBudgetPoolable::OnSpawnedFromPool`.

**`Refused` is climbing.**
You are at `MaxPopulation` or at `MaxQueueLength`. Raise the ceiling, lower `RingScale`, or accept
the refusal — it is being reported rather than dropped, which is the point.

**Milliseconds per tick spike anyway.**
One actor's construction script or `BeginPlay` is expensive. The ceiling is checked *between* items,
so a single actor can overshoot it by exactly one actor. Lower `MaxSpawnsPerFrame`, or make that
actor cheaper to construct — and note that with pooling on, it is only ever constructed once.

**The statistics box is not on screen.**
Either set the level's HUD class to `Spawn Budget HUD`, or tick `bAutoDrawStatsOnAnyHUD`. Check
`bShowStatsByDefault`. In a packaged build, both paths still work.

**The box is drawn twice.**
It cannot be: the `ASpawnBudgetHUD` path and the `OnHUDPostRender` path guard against each other with
a frame counter. If you see two boxes, you have two HUD actors.

---

## 13. Support

| | |
|---|---|
| **Documentation** | https://github.com/SimulatedFlow/ue-plugin-SpawnBudget |
| **Support** | teufelsilvan@gmail.com |
| **Publisher** | Silvan Teufel |

When reporting an issue, the output of `Spawn.Stats` and `Spawn.Sources` from the moment the problem
happens answers most of the first round of questions.

---

_© 2026 Silvan Teufel. All rights reserved. Licensed under the Fab Content License Agreement._
