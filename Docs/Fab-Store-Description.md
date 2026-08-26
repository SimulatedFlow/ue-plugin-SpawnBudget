# SpawnBudget — Fab Store Product Description

> Sales-ready copy. Sections map onto the Fab listing form: **Title**, **Short Description**,
> **Long Description**, **Technical Details**, **Price**.

---

## Title (Fab product name)

**SpawnBudget — Distance-Driven Spawning on a Frame Budget**

*Alternative, if the field is short:* `SpawnBudget — Budgeted World Spawner with Pooling`

---

## Headline

### A world spawner that cannot spike the frame — and shows you the number that proves it.

**Sub-headline:** At most N spawns and M despawns per frame. Three distance rings instead of one
radius. Every actor out of a pool. `New allocations` stops moving, on screen, in a Shipping build.

---

## One-paragraph pitch (Short Description)

Most spawners spawn everything that is due, and then you find out on the frame your level asks for
four hundred actors at once. SpawnBudget puts a hard ceiling on it: at most **N spawns and M despawns
per frame**, and never more than a set number of **milliseconds** — the rest waits in a queue sorted
by importance and continues on the next frame from exactly where it stopped. Nothing is lost, nothing
is spawned twice. What gets spawned is decided by **three distance rings** around the player, and the
gap between the first and the last is deliberate hysteresis, so a player pacing over a boundary
produces no flicker. What disappears goes back into a **per-class pool** instead of into garbage
collection. And the whole claim is a number you can read: **Pool hits against New allocations**,
drawn on a Canvas overlay that survives a cooked Shipping build. In the steady state the second
number stands still. That is the product — not that it spawns, but that it stops costing anything
once it has.

---

## Feature bullets (Long Description)

**A hard budget per frame — the point of the plugin**
* `MaxSpawnsPerFrame`, `MaxDespawnsPerFrame` and `MaxMillisecondsPerFrame`: three ceilings, because
  the three things that can go wrong are different — too many `SpawnActor` calls, too many destroys,
  and one unlucky construction script eating the frame on its own.
* The millisecond ceiling is checked *between* items, so one slow actor can overshoot it by exactly
  one actor and never by more.
* Run out of budget mid-frame and the loop stops where it is and continues from exactly there on the
  next one. The queue is still sorted, so what mattered most is still first.

**Three distance rings, not one radius**
* `SpawnIn` fills a source, `KeepAlive` holds it, `Despawn` takes actors back — the dead band between
  them is the hysteresis, measured in centimetres of level rather than in frames of smoothing, so it
  can be reasoned about on a map instead of tuned by feel.
* One global `RingScale` multiplier pulls the entire world's population in for a quality setting or a
  platform profile, without a designer having exposed a scalar on every actor in the level.

**Pooling that is actually free**
* A despawn hides the actor, drops its collision, and switches off the actor's tick **and the tick of
  every component that was running** — remembering which ones, so unparking restores the state it was
  in rather than whatever its class defaults say. A pool that leaves a movement component ticking is
  worse than no pool.
* No construction script runs, no components are re-registered, the garbage collector never hears
  about it.
* `ISpawnBudgetPoolable` (C++ **and** Blueprint) for actors with state the engine cannot reset —
  health, ammo, a state machine, a running timer. One code path for new and reused actors alike.

**Importance, so the right thing arrives first**
* The queue sorts by *distance ÷ source weight*: nearest and most important first, with arrival order
  breaking ties so equal requests stay FIFO.
* A request inside the camera cone counts double (configurable) — a multiplier on importance rather
  than a hard rule, because what is behind the player is exactly what they see when they turn round.

**A population ceiling that is honest about it**
* One world-wide ceiling. Above it, requests are **refused and counted**, never silently dropped.
  Lower the ceiling mid-play and the excess is queued for despawn, furthest away first.
* A refusal on the statistics box is a design decision. A request that vanishes is a bug report six
  weeks later about an empty village.

**Sources that just work**
* Drag in a **Spawn Source**, give it a class or three with pick weights, set how many there should
  be and how far away that stops mattering. No Blueprint, no trigger, no timer.
* Scatter over a **Point, Circle, Box or Spline**, optionally projected onto the **ground** (line
  trace) or the **navmesh** — so nothing spawns where nothing can walk.
* A scatter point that would land outside the despawn ring is redrawn rather than spawned, instead of
  paying full price to create an actor and take it away again within the second.
* **Sources do not tick.** Not one of them, ever. One subsystem walks them all once per frame, which
  is why a hundred sources cost about what one costs.

**Evidence, not adjectives**
* A `UCanvas` statistics box drawn from `AHUD`: population against ceiling, spawns and despawns *in
  this frame*, queue length, milliseconds per tick, **pool hits against new allocations**, refused
  requests and active sources.
* Nothing on it is estimated or smoothed — every field is the number the subsystem actually used on
  the frame you are reading.
* It is Canvas rather than UMG on purpose: it **survives a cooked Shipping build**. For a plugin whose
  whole claim is a number, a number that disappears in the build that ships is a number nobody can
  check.
* Already have a HUD class? Tick one setting and the same box draws through `AHUD::OnHUDPostRender`
  instead. No reparenting.

**Console commands for a ten-second proof**
* `Spawn.Stats`, `Spawn.Budget`, `Spawn.Population`, `Spawn.Clear`, `Spawn.Stress`, `Spawn.Pool`,
  `Spawn.Rings`, `Spawn.Sources`, `Spawn.Debug`.
* `Spawn.Budget 4` then `Spawn.Stress 1000` — the queue grows and drains evenly, milliseconds per
  tick stay flat. `Spawn.Budget 64`, same command — drained in seconds. Same job, measurably
  different curve.
* `Spawn.Pool 0` — watch `New allocations` climb. `Spawn.Pool 1` — watch it stand still.

**Blueprint-complete**
* Everything is exposed through a world-context-aware Blueprint library: `Request Spawn`,
  `Release Actor`, `Set Budget`, `Set Max Population`, `Get Population`, `Get Queue Length`,
  `Set Pooling Enabled`, `Set Ring Scale`, `Get Stats`, `Flush Queue`, and more.
* Every node is safe in a world with no SpawnBudget subsystem: queries answer nothing, setters do
  nothing, nobody crashes. A Blueprint written against it still runs in a test map where no source was
  ever placed.
* `OnActorSpawned` and `OnActorDespawned` delegates; the despawn event fires while the actor is still
  valid.

**Clean to integrate**
* One runtime module. **No UMG, no Niagara, no Chaos, no editor module, no plugin dependencies, no
  third-party code.**
* `Core`, `CoreUObject`, `Engine`, `DeveloperSettings` — plus `RenderCore` for the one-pixel texture
  the stats box is tiled from. That is the entire dependency list.
* Full C++ source included, heavily commented — every non-obvious decision has the reason written
  next to it.

---

## Why this instead of a bigger spawner

The competition sells **breadth**: schedules, probabilities, zones, multiplayer. All useful, none of
it an answer to the question that actually ships a game — *what does this cost on the frame it
happens, and can I prove it?*

SpawnBudget sells a **ceiling you can read**. One screen shows you the budget in force, what the last
frame actually spent, how much is still waiting, and whether the world has stopped allocating. If the
numbers do not convince you, do not buy it — that is the entire point of putting them on the HUD
rather than in this paragraph.

---

## Technical details (Fab "Technical Details" block)

**Features**
* Per-frame spawn / despawn / millisecond budget with a resumable, importance-sorted queue
* Three-ring distance model (`SpawnIn` / `KeepAlive` / `Despawn`) with explicit hysteresis
* Per-class actor pooling with component-tick save/restore and a `PoolHits` / `NewAllocations` counter
* World population ceiling with counted refusals
* Scatter shapes: Point, Circle, Box, Spline — with line-trace or navmesh ground projection
* `UCanvas` statistics overlay that works in a cooked Shipping build
* 9 console commands; full Blueprint library; `UDeveloperSettings` project settings page
* Poolable interface exposed to C++ and Blueprint

**Code Modules**
* `SpawnBudget` — Runtime — `LoadingPhase: PreDefault`

**Number of C++ Classes:** 7 (`ASpawnSource`, `USpawnBudgetSubsystem`, `USpawnBudgetPool`,
`USpawnBudgetStatics`, `USpawnBudgetSettings`, `ASpawnBudgetHUD`, `ISpawnBudgetPoolable`) plus 4
USTRUCTs and 2 UENUMs

**Network Replicated:** No (spawns through the standard `UWorld::SpawnActor` path, so replicated
actor classes replicate normally; there is no built-in client/server population negotiation)

**Supported Development Platforms:** Windows *(built and verified)*, macOS and Linux *(not listed in the plugin descriptor, not built for this release)*

**Supported Target Build Platforms:** Win64 *(built and verified)*, Mac and Linux *(not listed in the descriptor, not built for this release)*

**Supported Engine Versions:** Unreal Engine **5.8**

**Third-Party Dependencies:** None

**Plugin Dependencies:** None

**Documentation:** https://github.com/SimulatedFlow/ue-plugin-SpawnBudget

**Support:** teufelsilvan@gmail.com

**Important / Additional Notes:**
Mac and Linux are not declared in the plugin's `PlatformAllowList`; the code uses no platform-specific
APIs, but only Win64 was built and verified for this release — stated here rather than discovered
after purchase. The debug ring drawing is compiled out of a Shipping build (`ENABLE_DRAW_DEBUG`); the
statistics box and all console commands are not. The spawner runs in Game and PIE worlds only, by
design — an editor viewport belongs to the level designer.

---

## Target audience

* **Open-world and large-level teams** who already have a spawner and a hitching problem, and need
  the second one to stop without rewriting the first.
* **Survival, extraction, horde and wave shooters** — anything where a wave arriving is a frame spike
  waiting to happen.
* **Performance-conscious solo devs and small studios** who cannot afford a profiling week to find
  out whether population is what is costing them.
* **Console and Steam Deck ports**, where a single global `RingScale` and one budget call per quality
  tier is the difference between a scalability setting and a refactor.
* **Anyone migrating from `SpawnActor` in a Blueprint Tick**, which is the most common version of this
  problem and the one the pool counter makes visible in one click.

Assumes comfort with placing an actor and setting properties. **No C++ required** — the whole plugin
is usable from Blueprint. C++ users get commented source and a subsystem they can call directly.

---

## Price idea

**Recommended: `$14.99` USD** (≈ €14) — both license tiers (Personal and Professional / Fab Standard
and Enhanced), self-serve.

**Reasoning**

| Product | Price | Reviews | Sells |
|---|---|---|---|
| World Spawner — Global Spawning System | $149.99 | 26 (5.0★) | Breadth |
| Population Control PRO | $99.99 | 25 (4.9★) | Breadth |
| AI Spawner (MassEntity) | $39.99 | 0 | — |
| Procedural Instance Spawner | $89.99 | 0 | — |
| MasterSpawner / Wave Spawning System | $9.99 | 5 (3.2★) | The unhappy cheap tier |
| **SpawnBudget** | **$14.99** | — | **A measurable ceiling** |

Willingness to pay in this category is well established at **$99–$150**, so $14.99 is deliberately an
order of magnitude below the incumbents. That is the entry play: the two leaders sell scope, and a
new listing with no reviews does not win a scope argument. It wins a *single-claim* argument at an
impulse price, backed by a number on screen. $14.99 also sits clearly above the $9.99 tier that the
3.2★ product occupies, so it does not read as the cheap option.

**Launch:** consider a 25–30 % introductory discount (≈ $10.99) for the first two weeks to seed the
first reviews, which are the actual bottleneck in this category.

**Raise to $24.99–$29.99** once there are 10+ reviews at 4.5★ or better, or when a Mac/Linux verified
build and a multiplayer population story ship.

---

## Suggested store tags / keywords

`spawner`, `spawn system`, `wave spawner`, `object pooling`, `actor pool`, `performance`,
`optimization`, `open world`, `population`, `LOD`, `streaming`, `frame budget`, `distance culling`,
`crowd`, `AI spawning`, `blueprint`, `c++`

---

## Screenshot / video shot list (for the listing)

1. **The stats box, close up** — budget 4/frame, a queue of 900+, `New allocations` frozen while
   `Pool hits` runs. The single most important image on the page.
2. **Same shot, `Spawn.Pool 0`** — `New allocations` climbing. Side-by-side with (1) as one image.
3. **Budget 4 vs. budget 64** — split image of the queue length, same `Spawn.Stress 1000` command.
4. **`Spawn.Debug 1`** — the three rings drawn around several sources, with queued spawn points.
5. **The Details panel of a Spawn Source** — showing how little there is to configure.
6. **Project Settings → Plugins → SpawnBudget** — budget, pooling and source defaults.
7. **Video (60–90 s):** walk toward a source (fills at 4/frame) → walk away (despawns) → pace the ring
   boundary (no flicker) → `Spawn.Stress 1000` at budget 4 → raise to 64 → toggle the pool and let the
   viewer read the counter. Recorded from a `-game` window, not a PIE viewport.

---

_© 2026 Silvan Teufel. All rights reserved._
