// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "SpawnBudgetSubsystem.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GlobalRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "Misc/StringBuilder.h"
#include "SpawnBudgetLog.h"
#include "SpawnBudgetPool.h"
#include "SpawnBudgetPoolable.h"
#include "SpawnBudgetSettings.h"
#include "SpawnSource.h"

namespace SpawnBudgetPrivate
{
	/** Lines the statistics box always draws. */
	static constexpr int32 FixedStatsLines = 9;

	static constexpr float LineHeight = 15.0f;
	static constexpr float BoxPadding = 8.0f;

	/**
	 * How many queue entries one frame may look at while getting its spawn budget served.
	 *
	 * A refused request still costs a pop and a test, and with no millisecond ceiling set a queue full of
	 * refusals would be drained in one frame - the exact spike this plugin exists to prevent. The ceiling
	 * is generous enough that it is never reached while anything is actually being spawned.
	 */
	static constexpr int32 ServiceAttemptMultiplier = 4;

	static const FLinearColor PanelBackground(0.0f, 0.0f, 0.0f, 0.62f);
	static const FLinearColor HeadingColor(0.42f, 0.78f, 1.0f, 1.0f);
	static const FLinearColor BodyColor(0.9f, 0.9f, 0.9f, 1.0f);
	static const FLinearColor GoodColor(0.55f, 0.95f, 0.55f, 1.0f);
	static const FLinearColor WarnColor(0.98f, 0.78f, 0.35f, 1.0f);
	static const FLinearColor DimColor(0.62f, 0.62f, 0.62f, 1.0f);

	static void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, GWhiteTexture, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	static USpawnBudgetSubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<USpawnBudgetSubsystem>() : nullptr;
	}
}

//~ Lifetime -----------------------------------------------------------------------------------------------

void USpawnBudgetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ApplySettings();

	UE_LOG(LogSpawnBudget, Log,
		TEXT("SpawnBudget up: %s, budget %d spawns / %d despawns / %.2f ms, population ceiling %d, pooling %s."),
		bEnabled ? TEXT("enabled") : TEXT("disabled"),
		Limits.MaxSpawnsPerFrame, Limits.MaxDespawnsPerFrame, Limits.MaxMillisecondsPerFrame,
		MaxPopulation,
		bPoolingEnabled ? TEXT("on") : TEXT("off"));
}

void USpawnBudgetSubsystem::Deinitialize()
{
	if (HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}

	// The live actors belong to the world and go down with it. The parked ones are hidden and unticked,
	// which means nothing else is going to notice them - so they are destroyed explicitly rather than left
	// for a world teardown that might run after the pools have already been collected.
	for (TPair<TSubclassOf<AActor>, TObjectPtr<USpawnBudgetPool>>& Pair : Pools)
	{
		if (Pair.Value)
		{
			Pair.Value->DestroyParked();
		}
	}

	Pools.Reset();
	Sources.Reset();
	LiveActors.Reset();
	LiveIndexByActor.Reset();
	SpawnQueue.Reset();
	DespawnQueue.Reset();
	Viewers.Reset();

	Super::Deinitialize();
}

bool USpawnBudgetSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE, and deliberately not Editor. A level being built is not a level being played, and a
	// plugin that fills a designer's viewport with wandering actors is a plugin that gets removed.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USpawnBudgetSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USpawnBudgetSubsystem, STATGROUP_Tickables);
}

USpawnBudgetSubsystem* USpawnBudgetSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return SpawnBudgetPrivate::GetSubsystem(World);
}

void USpawnBudgetSubsystem::ApplySettings()
{
	const USpawnBudgetSettings& Settings = USpawnBudgetSettings::Get();

	bEnabled = Settings.bEnabled;
	Limits = Settings.Limits;
	MaxPopulation = FMath::Max(0, Settings.MaxPopulation);
	MaxQueueLength = FMath::Max(16, Settings.MaxQueueLength);
	MaxLiveScansPerFrame = FMath::Max(0, Settings.MaxLiveScansPerFrame);
	RescoreIntervalSeconds = FMath::Max(0.0f, Settings.RescoreIntervalSeconds);
	VisibleWeightMultiplier = FMath::Max(1.0f, Settings.VisibleWeightMultiplier);
	MaxPooledPerClass = FMath::Max(0, Settings.MaxPooledPerClass);
	bParkPooledActors = Settings.bParkPooledActors;
	ParkLocation = Settings.ParkLocation;
	bPoolingEnabled = Settings.bPoolingEnabled;
	bDebugDraw = Settings.bDebugDrawByDefault;
	bAutoDrawStatsOnAnyHUD = Settings.bAutoDrawStatsOnAnyHUD;

	DefaultRings = Settings.DefaultRings;
	DefaultRings.Sanitise();

	RebindHudDelegate();
}

//~ Sources ------------------------------------------------------------------------------------------------

void USpawnBudgetSubsystem::RegisterSource(ASpawnSource* Source)
{
	if (!IsValid(Source))
	{
		return;
	}

	const TWeakObjectPtr<ASpawnSource> WeakSource(Source);
	if (Sources.Contains(WeakSource))
	{
		return;
	}

	Source->Rings.Sanitise();
	Sources.Add(WeakSource);

	UE_LOG(LogSpawnBudget, Verbose, TEXT("SpawnBudget: registered source %s (max %d, rings %.0f/%.0f/%.0f)."),
		*Source->GetName(), Source->MaxAlive, Source->Rings.SpawnIn, Source->Rings.KeepAlive, Source->Rings.Despawn);
}

void USpawnBudgetSubsystem::UnregisterSource(ASpawnSource* Source)
{
	if (!Source)
	{
		return;
	}

	Sources.Remove(TWeakObjectPtr<ASpawnSource>(Source));

	// What it placed stays. Those actors are in the world, in view, and possibly being shot at; they leave
	// through their despawn ring like everything else, which is a fade rather than a cut.
}

void USpawnBudgetSubsystem::GetSources(TArray<ASpawnSource*>& OutSources) const
{
	OutSources.Reset();
	OutSources.Reserve(Sources.Num());

	for (const TWeakObjectPtr<ASpawnSource>& Weak : Sources)
	{
		if (ASpawnSource* Source = Weak.Get())
		{
			OutSources.Add(Source);
		}
	}
}

void USpawnBudgetSubsystem::DespawnSource(ASpawnSource* Source)
{
	if (!Source)
	{
		return;
	}

	for (int32 Index = 0; Index < LiveActors.Num(); ++Index)
	{
		if (LiveActors[Index].Source.Get() == Source)
		{
			QueueDespawn(Index, DistanceToNearestViewer(IsValid(LiveActors[Index].Actor) ? LiveActors[Index].Actor->GetActorLocation() : FVector::ZeroVector));
		}
	}
}

//~ Requests -----------------------------------------------------------------------------------------------

bool USpawnBudgetSubsystem::RequestSpawn(TSubclassOf<AActor> ActorClass, const FTransform& Transform, float Weight)
{
	return EnqueueSpawn(ActorClass, Transform, nullptr, Weight, DefaultRings);
}

bool USpawnBudgetSubsystem::RequestSpawnFromSource(TSubclassOf<AActor> ActorClass, const FTransform& Transform, ASpawnSource* Source)
{
	if (!Source)
	{
		return RequestSpawn(ActorClass, Transform, 1.0f);
	}

	return EnqueueSpawn(ActorClass, Transform, Source, Source->Weight, Source->Rings);
}

bool USpawnBudgetSubsystem::EnqueueSpawn(TSubclassOf<AActor> ActorClass, const FTransform& Transform, ASpawnSource* Source, float Weight, const FSpawnBudgetRings& Rings)
{
	if (!ActorClass)
	{
		return false;
	}

	if (SpawnQueue.Num() >= MaxQueueLength)
	{
		++RefusedCount;
		return false;
	}

	// The queue counts against the ceiling as well as the population does. A queue is a promise to spawn,
	// and promising more than the world is allowed to hold means the refusal arrives late - after the
	// request has been sorted, carried for a hundred frames and finally reached the front.
	if (LiveActors.Num() + SpawnQueue.Num() >= MaxPopulation)
	{
		++RefusedCount;
		return false;
	}

	FSpawnBudgetPendingSpawn Request;
	Request.ActorClass = ActorClass;
	Request.Transform = Transform;
	Request.Source = Source;
	Request.Rings = Rings;
	Request.Weight = FMath::Max(0.01f, Weight);
	Request.Serial = NextSerial++;
	Request.Score = ComputeScore(Transform.GetLocation(), Request.Weight, Request.bVisible);

	SpawnQueue.Add(MoveTemp(Request));
	bSpawnQueueDirty = true;

	if (Source)
	{
		++Source->PendingCount;
	}

	return true;
}

void USpawnBudgetSubsystem::ReleaseActor(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return;
	}

	// Not ours is not an error. A game that calls this from one shared death handler for every actor it
	// has is doing exactly the right thing, and warning about the ones this plugin did not spawn would
	// make that impossible.
	if (const int32* IndexPtr = LiveIndexByActor.Find(FObjectKey(Actor)))
	{
		QueueDespawn(*IndexPtr, DistanceToNearestViewer(Actor->GetActorLocation()));
	}
}

int32 USpawnBudgetSubsystem::RequestStress(int32 Count)
{
	if (Count <= 0)
	{
		return 0;
	}

	TArray<ASpawnSource*> UsableSources;
	GetSources(UsableSources);
	UsableSources.RemoveAll([](const ASpawnSource* Source)
	{
		return !Source || Source->Classes.Num() == 0;
	});

	if (UsableSources.Num() == 0)
	{
		UE_LOG(LogSpawnBudget, Warning, TEXT("SpawnBudget: stress test asked for %d actors, but no source in this world has a class to spawn."), Count);
		return 0;
	}

	TArray<FVector> ViewerLocations;
	ViewerLocations.Reserve(Viewers.Num());
	for (const FSpawnBudgetViewer& Viewer : Viewers)
	{
		ViewerLocations.Add(Viewer.Location);
	}

	int32 Queued = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		ASpawnSource* Source = UsableSources[Index % UsableSources.Num()];

		FTransform Transform;
		if (!Source->BuildSpawnTransform(Transform, ViewerLocations, RingScale))
		{
			continue;
		}

		// Deliberately unowned: a stress request borrows a source's scatter shape but is not part of its
		// population, so it neither counts against MaxAlive nor stops the source topping itself up. What
		// it does count against is the world ceiling and the frame budget, which is the whole point of
		// throwing a thousand of them at the queue at once.
		if (EnqueueSpawn(Source->PickClass(), Transform, nullptr, Source->Weight, DefaultRings))
		{
			++Queued;
		}
	}

	UE_LOG(LogSpawnBudget, Display, TEXT("SpawnBudget: stress test queued %d of %d requests (queue is now %d)."),
		Queued, Count, SpawnQueue.Num());

	return Queued;
}

void USpawnBudgetSubsystem::FlushQueue()
{
	for (const FSpawnBudgetPendingSpawn& Request : SpawnQueue)
	{
		if (ASpawnSource* Source = Request.Source.Get())
		{
			Source->PendingCount = FMath::Max(0, Source->PendingCount - 1);
		}
	}

	FlushedCount += SpawnQueue.Num();

	UE_LOG(LogSpawnBudget, Display, TEXT("SpawnBudget: flushed %d queued request(s). Nothing already alive was touched."),
		SpawnQueue.Num());

	SpawnQueue.Reset();
	bSpawnQueueDirty = false;
}

void USpawnBudgetSubsystem::ClearAll()
{
	FlushQueue();

	DespawnQueue.Reset();
	bDespawnQueueDirty = false;

	// Backwards, so RemoveAtSwap can never move an entry we have not looked at yet into a slot we have.
	for (int32 Index = LiveActors.Num() - 1; Index >= 0; --Index)
	{
		DespawnLiveActor(Index);
	}

	for (TPair<TSubclassOf<AActor>, TObjectPtr<USpawnBudgetPool>>& Pair : Pools)
	{
		if (Pair.Value)
		{
			Pair.Value->DestroyParked();
		}
	}

	for (const TWeakObjectPtr<ASpawnSource>& Weak : Sources)
	{
		if (ASpawnSource* Source = Weak.Get())
		{
			Source->LiveCount = 0;
			Source->PendingCount = 0;
			Source->TimeUntilRefill = 0.0f;
		}
	}

	UE_LOG(LogSpawnBudget, Display, TEXT("SpawnBudget: cleared. Population 0, queues empty, pools emptied."));
}

//~ Budget -------------------------------------------------------------------------------------------------

void USpawnBudgetSubsystem::SetBudget(int32 MaxSpawnsPerFrame, int32 MaxDespawnsPerFrame, float MaxMillisecondsPerFrame)
{
	// Negative leaves a ceiling alone, so a caller can change one of the three without having to know the
	// other two - which is what a button on a debug panel wants.
	if (MaxSpawnsPerFrame >= 0)
	{
		Limits.MaxSpawnsPerFrame = MaxSpawnsPerFrame;
	}

	if (MaxDespawnsPerFrame >= 0)
	{
		Limits.MaxDespawnsPerFrame = MaxDespawnsPerFrame;
	}

	if (MaxMillisecondsPerFrame >= 0.0f)
	{
		Limits.MaxMillisecondsPerFrame = MaxMillisecondsPerFrame;
	}

	UE_LOG(LogSpawnBudget, Display, TEXT("SpawnBudget: budget is now %d spawns / %d despawns / %.2f ms per frame."),
		Limits.MaxSpawnsPerFrame, Limits.MaxDespawnsPerFrame, Limits.MaxMillisecondsPerFrame);
}

void USpawnBudgetSubsystem::SetMaxPopulation(int32 NewMaxPopulation)
{
	MaxPopulation = FMath::Max(0, NewMaxPopulation);

	const int32 Excess = LiveActors.Num() - MaxPopulation;
	if (Excess <= 0)
	{
		return;
	}

	// Lowering the ceiling under a world that is already fuller than that has to take actors away, and
	// which ones is not arbitrary: furthest from the player first, so the population comes down out of
	// sight. They go through the normal despawn queue and the normal budget, so lowering the ceiling by
	// four thousand still does not cost a frame.
	TArray<TPair<float, int32>> ByDistance;
	ByDistance.Reserve(LiveActors.Num());

	for (int32 Index = 0; Index < LiveActors.Num(); ++Index)
	{
		const FSpawnBudgetLiveActor& Entry = LiveActors[Index];
		if (Entry.bDespawnQueued || !IsValid(Entry.Actor))
		{
			continue;
		}

		ByDistance.Emplace(DistanceToNearestViewer(Entry.Actor->GetActorLocation()), Index);
	}

	ByDistance.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B)
	{
		return A.Key > B.Key;
	});

	const int32 ToQueue = FMath::Min(Excess, ByDistance.Num());
	for (int32 Index = 0; Index < ToQueue; ++Index)
	{
		QueueDespawn(ByDistance[Index].Value, ByDistance[Index].Key);
	}

	UE_LOG(LogSpawnBudget, Display, TEXT("SpawnBudget: population ceiling is now %d; queued %d actor(s) for despawn, furthest first."),
		MaxPopulation, ToQueue);
}

//~ Switches -----------------------------------------------------------------------------------------------

void USpawnBudgetSubsystem::SetPoolingEnabled(bool bNewEnabled)
{
	if (bPoolingEnabled == bNewEnabled)
	{
		return;
	}

	bPoolingEnabled = bNewEnabled;

	// Switching pooling off empties the pools rather than leaving them parked. Leaving them would mean the
	// parked-actor count on the statistics box stayed high while nothing could ever be served from it,
	// which reads as pooling still working when it is not.
	if (!bPoolingEnabled)
	{
		for (TPair<TSubclassOf<AActor>, TObjectPtr<USpawnBudgetPool>>& Pair : Pools)
		{
			if (Pair.Value)
			{
				Pair.Value->DestroyParked();
			}
		}
	}

	UE_LOG(LogSpawnBudget, Display, TEXT("SpawnBudget: pooling %s."), bPoolingEnabled ? TEXT("on") : TEXT("off - every despawn now destroys and every spawn allocates"));
}

void USpawnBudgetSubsystem::SetRingScale(float NewRingScale)
{
	RingScale = FMath::Clamp(NewRingScale, 0.01f, 100.0f);

	UE_LOG(LogSpawnBudget, Display, TEXT("SpawnBudget: rings scaled by %.2f."), RingScale);
}

void USpawnBudgetSubsystem::SetEnabled(bool bNewEnabled)
{
	bEnabled = bNewEnabled;
}

void USpawnBudgetSubsystem::SetDebugDraw(bool bNewEnabled)
{
	bDebugDraw = bNewEnabled;
}

//~ Tick ---------------------------------------------------------------------------------------------------

void USpawnBudgetSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!GetWorld())
	{
		return;
	}

	const double TickStart = FPlatformTime::Seconds();

	Stats.SpawnsThisFrame = 0;
	Stats.DespawnsThisFrame = 0;
	Stats.bBudgetSaturated = false;
	Stats.bTimeSliceHit = false;
	Stats.ServiceMilliseconds = 0.0f;

	UpdateViewers();

	if (bEnabled)
	{
		EvaluateSources(DeltaTime);
		ScanLiveActors();
		RescoreSpawnQueue(DeltaTime);
		ServiceQueues();
	}

	UpdateStats(TickStart);

	if (bDebugDraw)
	{
		DrawDebugRings();
	}
}

void USpawnBudgetSubsystem::UpdateViewers()
{
	Viewers.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (!PlayerController || !PlayerController->IsLocalController())
		{
			continue;
		}

		FSpawnBudgetViewer Viewer;

		// The camera, not the pawn. A pawn in a third-person game is behind the camera by several metres,
		// and in a spectator or fly-through it may be nowhere near it at all; what decides whether an
		// actor is worth having is where the world is being looked at from.
		if (const APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
		{
			Viewer.Location = CameraManager->GetCameraLocation();
			Viewer.Forward = CameraManager->GetCameraRotation().Vector();

			const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(CameraManager->GetFOVAngle(), 1.0f, 179.0f) * 0.5f);
			Viewer.CosHalfFov = FMath::Cos(HalfFov);
		}
		else if (const APawn* Pawn = PlayerController->GetPawn())
		{
			Viewer.Location = Pawn->GetActorLocation();
			Viewer.Forward = Pawn->GetActorForwardVector();
			Viewer.CosHalfFov = FMath::Cos(FMath::DegreesToRadians(45.0f));
		}
		else
		{
			continue;
		}

		Viewers.Add(Viewer);
	}
}

void USpawnBudgetSubsystem::EvaluateSources(float DeltaTime)
{
	int32 ActiveSources = 0;

	TArray<FVector> ViewerLocations;
	ViewerLocations.Reserve(Viewers.Num());
	for (const FSpawnBudgetViewer& Viewer : Viewers)
	{
		ViewerLocations.Add(Viewer.Location);
	}

	for (int32 Index = Sources.Num() - 1; Index >= 0; --Index)
	{
		ASpawnSource* Source = Sources[Index].Get();
		if (!IsValid(Source))
		{
			Sources.RemoveAtSwap(Index, EAllowShrinking::No);
			continue;
		}

		const float Distance = DistanceToNearestViewer(Source->GetActorLocation());
		Source->LastViewerDistance = Distance;

		const float Scale = FMath::Max(0.01f, RingScale);
		Source->bInRange = Distance <= Source->Rings.KeepAlive * Scale;
		ActiveSources += Source->bInRange ? 1 : 0;

		if (Source->RefillIntervalSeconds > 0.0f)
		{
			Source->TimeUntilRefill = FMath::Max(0.0f, Source->TimeUntilRefill - DeltaTime);
		}

		if (!Source->bSourceEnabled)
		{
			continue;
		}

		// Outside SpawnIn nothing new is asked for. Between SpawnIn and KeepAlive the source holds what it
		// has, and past KeepAlive it holds it still - taking actors away is the job of their own Despawn
		// ring, one ring further out. That gap is the hysteresis, and it is why walking a boundary does
		// not produce a spawn/despawn cycle.
		if (Distance > Source->Rings.SpawnIn * Scale)
		{
			continue;
		}

		if (Source->TimeUntilRefill > 0.0f)
		{
			continue;
		}

		const int32 Deficit = Source->MaxAlive - Source->LiveCount - Source->PendingCount;
		if (Deficit <= 0)
		{
			continue;
		}

		const int32 ToQueue = FMath::Min(Deficit, FMath::Max(1, Source->MaxRequestsPerEvaluation));
		int32 Queued = 0;

		for (int32 Request = 0; Request < ToQueue; ++Request)
		{
			FTransform Transform;
			if (!Source->BuildSpawnTransform(Transform, ViewerLocations, Scale))
			{
				// Nowhere usable to put it this frame - the whole scatter area is out of range, off the
				// navmesh, or over a hole. Try again next frame rather than dropping something in a wall.
				break;
			}

			if (!RequestSpawnFromSource(Source->PickClass(), Transform, Source))
			{
				break;
			}

			++Queued;
		}

		if (Queued > 0 && Source->RefillIntervalSeconds > 0.0f)
		{
			Source->TimeUntilRefill = Source->RefillIntervalSeconds;
		}
	}

	Stats.ActiveSources = ActiveSources;
}

void USpawnBudgetSubsystem::ScanLiveActors()
{
	if (LiveActors.Num() == 0)
	{
		LiveScanCursor = 0;
		return;
	}

	// A rolling window, not the whole population. The test is one squared distance, but paying it ten
	// thousand times a frame buys nothing: nothing crosses a sixty-metre ring in a sixtieth of a second,
	// so a despawn that arrives four frames later than it could have is a despawn nobody can see. What
	// this does buy is a scan cost that does not grow with the population.
	const int32 ToScan = MaxLiveScansPerFrame <= 0
		? LiveActors.Num()
		: FMath::Min(MaxLiveScansPerFrame, LiveActors.Num());

	const float Scale = FMath::Max(0.01f, RingScale);

	for (int32 Scanned = 0; Scanned < ToScan; ++Scanned)
	{
		if (LiveActors.Num() == 0)
		{
			break;
		}

		if (!LiveActors.IsValidIndex(LiveScanCursor))
		{
			LiveScanCursor = 0;
		}

		const int32 Index = LiveScanCursor;
		FSpawnBudgetLiveActor& Entry = LiveActors[Index];

		if (!IsValid(Entry.Actor))
		{
			// Destroyed behind our back - a level unload, a Blueprint being helpful, a kill volume. Take
			// it off the books silently; the cursor stays put because RemoveAtSwap has just moved an
			// unexamined entry into this slot.
			if (ASpawnSource* Source = Entry.Source.Get())
			{
				Source->LiveCount = FMath::Max(0, Source->LiveCount - 1);
			}

			RemoveLiveAt(Index);
			continue;
		}

		if (!Entry.bDespawnQueued)
		{
			const float Distance = DistanceToNearestViewer(Entry.Actor->GetActorLocation());
			if (Distance > Entry.Rings.Despawn * Scale)
			{
				QueueDespawn(Index, Distance);
			}
		}

		++LiveScanCursor;
	}
}

void USpawnBudgetSubsystem::RescoreSpawnQueue(float DeltaTime)
{
	if (SpawnQueue.Num() == 0)
	{
		return;
	}

	TimeUntilRescore -= DeltaTime;

	if (RescoreIntervalSeconds <= 0.0f || TimeUntilRescore <= 0.0f)
	{
		// A score is a distance, and the player has moved since it was taken. Re-scoring is what keeps
		// "nearest first" true for a queue that has been waiting several seconds.
		for (FSpawnBudgetPendingSpawn& Request : SpawnQueue)
		{
			Request.Score = ComputeScore(Request.Transform.GetLocation(), Request.Weight, Request.bVisible);
		}

		TimeUntilRescore = RescoreIntervalSeconds;
		bSpawnQueueDirty = true;
	}

	// Nothing to decide when the whole queue fits in this frame's budget anyway.
	if (!bSpawnQueueDirty || SpawnQueue.Num() <= Limits.MaxSpawnsPerFrame)
	{
		return;
	}

	// Sorted worst first, so that taking the best is a Pop from the back and costs nothing.
	SpawnQueue.Sort([](const FSpawnBudgetPendingSpawn& A, const FSpawnBudgetPendingSpawn& B)
	{
		if (A.Score != B.Score)
		{
			return A.Score > B.Score;
		}

		// Equal scores keep their arrival order, so a request cannot be overtaken forever by an endless
		// supply of things exactly as important as it is.
		return A.Serial > B.Serial;
	});

	bSpawnQueueDirty = false;
}

void USpawnBudgetSubsystem::ServiceQueues()
{
	const double ServiceStart = FPlatformTime::Seconds();
	const double Deadline = Limits.MaxMillisecondsPerFrame > 0.0f
		? ServiceStart + static_cast<double>(Limits.MaxMillisecondsPerFrame) / 1000.0
		: TNumericLimits<double>::Max();

	if (bDespawnQueueDirty && DespawnQueue.Num() > 1)
	{
		// Ascending, so the furthest away - the one nobody can see going - is at the back and is popped
		// first.
		DespawnQueue.Sort([](const FSpawnBudgetPendingDespawn& A, const FSpawnBudgetPendingDespawn& B)
		{
			return A.Score < B.Score;
		});

		bDespawnQueueDirty = false;
	}

	// Despawns first. They are cheaper, and each one frees a slot under the population ceiling that a
	// spawn in the same frame can use - which is what makes moving through a level cost nothing net.
	int32 Despawns = 0;
	int32 Attempts = 0;
	const int32 MaxDespawnAttempts = FMath::Max(16, Limits.MaxDespawnsPerFrame * SpawnBudgetPrivate::ServiceAttemptMultiplier);

	while (DespawnQueue.Num() > 0 && Despawns < Limits.MaxDespawnsPerFrame && Attempts < MaxDespawnAttempts)
	{
		if (FPlatformTime::Seconds() >= Deadline)
		{
			Stats.bTimeSliceHit = true;
			break;
		}

		++Attempts;
		if (ServiceOneDespawn())
		{
			++Despawns;
		}
	}

	int32 Spawns = 0;
	Attempts = 0;
	const int32 MaxSpawnAttempts = FMath::Max(16, Limits.MaxSpawnsPerFrame * SpawnBudgetPrivate::ServiceAttemptMultiplier);

	while (SpawnQueue.Num() > 0 && Spawns < Limits.MaxSpawnsPerFrame && Attempts < MaxSpawnAttempts)
	{
		if (FPlatformTime::Seconds() >= Deadline)
		{
			Stats.bTimeSliceHit = true;
			break;
		}

		++Attempts;
		if (ServiceOneSpawn())
		{
			++Spawns;
		}
	}

	Stats.SpawnsThisFrame = Spawns;
	Stats.DespawnsThisFrame = Despawns;
	Stats.bBudgetSaturated = SpawnQueue.Num() > 0 || DespawnQueue.Num() > 0;
	Stats.ServiceMilliseconds = static_cast<float>((FPlatformTime::Seconds() - ServiceStart) * 1000.0);
}

bool USpawnBudgetSubsystem::ServiceOneSpawn()
{
	FSpawnBudgetPendingSpawn Request = SpawnQueue.Pop(EAllowShrinking::No);

	ASpawnSource* Source = Request.Source.Get();
	if (Source)
	{
		Source->PendingCount = FMath::Max(0, Source->PendingCount - 1);
	}

	if (!Request.ActorClass)
	{
		return false;
	}

	// Checked again here as well as at enqueue time, because the ceiling can have been lowered, or other
	// requests can have been served, in the frames this one spent waiting.
	if (LiveActors.Num() >= MaxPopulation)
	{
		++RefusedCount;
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	AActor* Actor = nullptr;
	bool bPoolHit = false;

	if (bPoolingEnabled)
	{
		if (USpawnBudgetPool* Pool = FindOrCreatePool(Request.ActorClass))
		{
			Actor = Pool->Acquire(World, Request.Transform, Source, Source, bPoolHit);
		}
	}
	else
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = Source;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;

		Actor = World->SpawnActor<AActor>(Request.ActorClass, Request.Transform, SpawnParams);
		if (Actor)
		{
			++DirectAllocations;

			if (Actor->Implements<USpawnBudgetPoolable>())
			{
				ISpawnBudgetPoolable::Execute_OnSpawnedFromPool(Actor, Source, false);
			}
		}
	}

	if (!Actor)
	{
		return false;
	}

	FSpawnBudgetLiveActor Live;
	Live.Actor = Actor;
	Live.Source = Source;
	Live.ActorClass = Request.ActorClass;
	Live.Rings = Request.Rings;
	Live.Key = FObjectKey(Actor);

	const int32 LiveIndex = LiveActors.Add(MoveTemp(Live));
	LiveIndexByActor.Add(FObjectKey(Actor), LiveIndex);

	if (Source)
	{
		++Source->LiveCount;
	}

	++TotalSpawned;

	OnActorSpawned.Broadcast(Actor, Source);

	return true;
}

bool USpawnBudgetSubsystem::ServiceOneDespawn()
{
	const FSpawnBudgetPendingDespawn Pending = DespawnQueue.Pop(EAllowShrinking::No);

	AActor* Actor = Pending.Actor.Get();
	if (!IsValid(Actor))
	{
		return false;
	}

	const int32* IndexPtr = LiveIndexByActor.Find(FObjectKey(Actor));
	if (!IndexPtr || !LiveActors.IsValidIndex(*IndexPtr))
	{
		return false;
	}

	DespawnLiveActor(*IndexPtr);
	return true;
}

void USpawnBudgetSubsystem::QueueDespawn(int32 LiveIndex, float Score)
{
	if (!LiveActors.IsValidIndex(LiveIndex))
	{
		return;
	}

	FSpawnBudgetLiveActor& Entry = LiveActors[LiveIndex];
	if (Entry.bDespawnQueued || !IsValid(Entry.Actor))
	{
		return;
	}

	Entry.bDespawnQueued = true;

	FSpawnBudgetPendingDespawn Pending;
	Pending.Actor = Entry.Actor;
	Pending.Score = Score;

	DespawnQueue.Add(Pending);
	bDespawnQueueDirty = true;
}

void USpawnBudgetSubsystem::DespawnLiveActor(int32 LiveIndex)
{
	if (!LiveActors.IsValidIndex(LiveIndex))
	{
		return;
	}

	// Copied, because parking the actor can run game code that ends up back in here.
	const FSpawnBudgetLiveActor Entry = LiveActors[LiveIndex];
	ASpawnSource* Source = Entry.Source.Get();
	AActor* Actor = Entry.Actor;

	RemoveLiveAt(LiveIndex);

	if (Source)
	{
		Source->LiveCount = FMath::Max(0, Source->LiveCount - 1);
	}

	if (IsValid(Actor))
	{
		// Announced while it is still whole, so a listener can read its transform, its health, its name.
		OnActorDespawned.Broadcast(Actor, Source);

		bool bParked = false;
		if (bPoolingEnabled)
		{
			if (USpawnBudgetPool* Pool = FindOrCreatePool(Entry.ActorClass))
			{
				bParked = Pool->Release(Actor);
			}
		}

		if (!bParked)
		{
			Actor->Destroy();
			++DirectDestroys;
		}
	}

	++TotalDespawned;
}

void USpawnBudgetSubsystem::RemoveLiveAt(int32 Index)
{
	if (!LiveActors.IsValidIndex(Index))
	{
		return;
	}

	LiveIndexByActor.Remove(LiveActors[Index].Key);
	LiveActors.RemoveAtSwap(Index, EAllowShrinking::No);

	// RemoveAtSwap moved the last entry into this slot - its index in the map is now wrong.
	if (LiveActors.IsValidIndex(Index))
	{
		LiveIndexByActor.Add(LiveActors[Index].Key, Index);
	}
}

//~ Scoring ------------------------------------------------------------------------------------------------

float USpawnBudgetSubsystem::DistanceToNearestViewer(const FVector& Point, bool* bOutVisible) const
{
	if (bOutVisible)
	{
		*bOutVisible = false;
	}

	if (Viewers.Num() == 0)
	{
		// No local player yet - during startup, during a seamless travel, in a dedicated-server world.
		// Everything is infinitely far away, so nothing spawns and nothing despawns. That is the correct
		// answer rather than a special case: as soon as there is somebody to populate the world for, the
		// rings do it.
		return TNumericLimits<float>::Max();
	}

	float Best = TNumericLimits<float>::Max();
	bool bVisible = false;

	for (const FSpawnBudgetViewer& Viewer : Viewers)
	{
		const FVector Offset = Point - Viewer.Location;
		const float Distance = static_cast<float>(Offset.Size());

		if (Distance < Best)
		{
			Best = Distance;
		}

		if (bOutVisible && !bVisible && Distance > KINDA_SMALL_NUMBER)
		{
			const float Cosine = static_cast<float>(FVector::DotProduct(Offset / Distance, Viewer.Forward));
			bVisible = Cosine >= Viewer.CosHalfFov;
		}
	}

	if (bOutVisible)
	{
		*bOutVisible = bVisible;
	}

	return Best;
}

float USpawnBudgetSubsystem::ComputeScore(const FVector& Location, float Weight, bool& bOutVisible) const
{
	const float Distance = DistanceToNearestViewer(Location, &bOutVisible);

	// Distance divided by importance, so a source at twice the weight behaves as though it were half as
	// far away, and something inside the camera cone counts as nearer again. Lower is sooner.
	const float EffectiveWeight = FMath::Max(0.01f, Weight * (bOutVisible ? VisibleWeightMultiplier : 1.0f));
	return Distance / EffectiveWeight;
}

USpawnBudgetPool* USpawnBudgetSubsystem::FindOrCreatePool(TSubclassOf<AActor> ActorClass)
{
	if (!ActorClass)
	{
		return nullptr;
	}

	if (TObjectPtr<USpawnBudgetPool>* Existing = Pools.Find(ActorClass))
	{
		if (*Existing)
		{
			// Project settings can have changed since the pool was made - a scalability profile, a
			// console command, a designer in the Project Settings window.
			(*Existing)->ApplyLimits(MaxPooledPerClass, bParkPooledActors, ParkLocation);
			return *Existing;
		}
	}

	USpawnBudgetPool* Pool = NewObject<USpawnBudgetPool>(this);
	Pool->Initialise(ActorClass, MaxPooledPerClass, bParkPooledActors, ParkLocation);
	Pools.Add(ActorClass, Pool);

	return Pool;
}

//~ Statistics ---------------------------------------------------------------------------------------------

void USpawnBudgetSubsystem::UpdateStats(double TickStartSeconds)
{
	Stats.Population = LiveActors.Num();
	Stats.MaxPopulation = MaxPopulation;
	Stats.SpawnQueueLength = SpawnQueue.Num();
	Stats.DespawnQueueLength = DespawnQueue.Num();
	Stats.Refused = RefusedCount;
	Stats.RegisteredSources = Sources.Num();
	Stats.bPoolingEnabled = bPoolingEnabled;
	Stats.RingScale = RingScale;
	Stats.Limits = Limits;

	int32 Hits = 0;
	int32 News = DirectAllocations;
	int32 Parked = 0;

	for (const TPair<TSubclassOf<AActor>, TObjectPtr<USpawnBudgetPool>>& Pair : Pools)
	{
		if (const USpawnBudgetPool* Pool = Pair.Value)
		{
			Hits += Pool->GetPoolHits();
			News += Pool->GetNewAllocations();
			Parked += Pool->GetParkedCount();
		}
	}

	Stats.PoolHits = Hits;
	Stats.NewAllocations = News;
	Stats.PooledActors = Parked;

	Stats.TickMilliseconds = static_cast<float>((FPlatformTime::Seconds() - TickStartSeconds) * 1000.0);
}

int32 USpawnBudgetSubsystem::GetStatsLineCount() const
{
	return SpawnBudgetPrivate::FixedStatsLines;
}

void USpawnBudgetSubsystem::DrawStatsBox(UCanvas* Canvas, const FVector2D& Origin, float Width) const
{
	using namespace SpawnBudgetPrivate;

	if (!Canvas)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	LastStatsDrawFrame = GFrameCounter;

	const float BoxHeight = GetStatsLineCount() * LineHeight + BoxPadding * 2.0f;
	DrawFilledRect(Canvas,
		FVector2D(Origin.X - BoxPadding, Origin.Y - BoxPadding),
		FVector2D(Width, BoxHeight),
		PanelBackground);

	float LineY = static_cast<float>(Origin.Y);
	auto DrawLine = [&](FStringView Line, const FLinearColor& Color)
	{
		FCanvasTextStringViewItem Item(FVector2D(Origin.X, LineY), Line, Font, Color);
		Canvas->DrawItem(Item);
		LineY += LineHeight;
	};

	TStringBuilder<192> Line;

	Line.Reset();
	Line.Appendf(TEXT("SpawnBudget%s"), bEnabled ? TEXT("") : TEXT("   (disabled)"));
	DrawLine(Line.ToView(), bEnabled ? HeadingColor : DimColor);

	Line.Reset();
	Line.Appendf(TEXT("Population     %d / %d"), Stats.Population, Stats.MaxPopulation);
	DrawLine(Line.ToView(), Stats.Population >= Stats.MaxPopulation ? WarnColor : BodyColor);

	// Spawns and despawns for THIS frame, next to the ceiling they are being measured against. A total
	// would look impressive and prove nothing; what has to be shown is that the per-frame number never
	// goes above the number beside it.
	Line.Reset();
	Line.Appendf(TEXT("Spawns         %d this frame   (budget %d)"), Stats.SpawnsThisFrame, Stats.Limits.MaxSpawnsPerFrame);
	DrawLine(Line.ToView(), Stats.SpawnsThisFrame > 0 ? GoodColor : BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Despawns       %d this frame   (budget %d)"), Stats.DespawnsThisFrame, Stats.Limits.MaxDespawnsPerFrame);
	DrawLine(Line.ToView(), Stats.DespawnsThisFrame > 0 ? GoodColor : BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Queue          %d waiting   %d leaving%s"),
		Stats.SpawnQueueLength,
		Stats.DespawnQueueLength,
		Stats.bBudgetSaturated ? TEXT("   [budgeted]") : TEXT(""));
	DrawLine(Line.ToView(), Stats.SpawnQueueLength > 0 ? WarnColor : BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Milliseconds   %.3f service / %.3f tick   (budget %.2f)%s"),
		Stats.ServiceMilliseconds,
		Stats.TickMilliseconds,
		Stats.Limits.MaxMillisecondsPerFrame,
		Stats.bTimeSliceHit ? TEXT("  [cut]") : TEXT(""));
	DrawLine(Line.ToView(), Stats.bTimeSliceHit ? WarnColor : BodyColor);

	// The line the product is sold on. New allocations must stop moving while pool hits keep climbing.
	Line.Reset();
	Line.Appendf(TEXT("Pool %s   hits %d   new %d   parked %d"),
		Stats.bPoolingEnabled ? TEXT("ON ") : TEXT("OFF"),
		Stats.PoolHits,
		Stats.NewAllocations,
		Stats.PooledActors);
	DrawLine(Line.ToView(), Stats.bPoolingEnabled ? GoodColor : WarnColor);

	Line.Reset();
	Line.Appendf(TEXT("Refused        %d"), Stats.Refused);
	DrawLine(Line.ToView(), Stats.Refused > 0 ? WarnColor : DimColor);

	Line.Reset();
	Line.Appendf(TEXT("Sources        %d of %d in range   rings x%.2f"),
		Stats.ActiveSources, Stats.RegisteredSources, Stats.RingScale);
	DrawLine(Line.ToView(), Stats.ActiveSources > 0 ? BodyColor : DimColor);
}

void USpawnBudgetSubsystem::RebindHudDelegate()
{
	if (bAutoDrawStatsOnAnyHUD && !HudPostRenderHandle.IsValid())
	{
		HudPostRenderHandle = AHUD::OnHUDPostRender.AddUObject(this, &USpawnBudgetSubsystem::OnAnyHUDPostRender);
	}
	else if (!bAutoDrawStatsOnAnyHUD && HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}
}

void USpawnBudgetSubsystem::OnAnyHUDPostRender(AHUD* HUD, UCanvas* Canvas)
{
	if (!bAutoDrawStatsOnAnyHUD || !HUD || !Canvas)
	{
		return;
	}

	if (HUD->GetWorld() != GetWorld())
	{
		return;
	}

	// ASpawnBudgetHUD already drew this frame - do not stack a second box on top of it.
	if (LastStatsDrawFrame == GFrameCounter)
	{
		return;
	}

	DrawStatsBox(Canvas, FVector2D(28.0f, 90.0f), 420.0f);
}

void USpawnBudgetSubsystem::DrawDebugRings() const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Scale = FMath::Max(0.01f, RingScale);

	for (const TWeakObjectPtr<ASpawnSource>& Weak : Sources)
	{
		const ASpawnSource* Source = Weak.Get();
		if (!IsValid(Source))
		{
			continue;
		}

		const FVector Location = Source->GetActorLocation();

		DrawDebugCircle(World, Location, Source->Rings.SpawnIn * Scale, 64, FColor(80, 220, 100), false, -1.0f, 0, 4.0f,
			FVector(1, 0, 0), FVector(0, 1, 0), false);
		DrawDebugCircle(World, Location, Source->Rings.KeepAlive * Scale, 64, FColor(240, 200, 90), false, -1.0f, 0, 4.0f,
			FVector(1, 0, 0), FVector(0, 1, 0), false);
		DrawDebugCircle(World, Location, Source->Rings.Despawn * Scale, 64, FColor(230, 90, 80), false, -1.0f, 0, 4.0f,
			FVector(1, 0, 0), FVector(0, 1, 0), false);
	}

	// Where the queue is about to put things. Seeing the waiting requests is what makes a budget visible
	// rather than theoretical.
	const int32 DrawCount = FMath::Min(SpawnQueue.Num(), 256);
	for (int32 Index = SpawnQueue.Num() - DrawCount; Index < SpawnQueue.Num(); ++Index)
	{
		DrawDebugPoint(World, SpawnQueue[Index].Transform.GetLocation(), 12.0f, FColor(120, 190, 255), false, -1.0f, 0);
	}
#endif // ENABLE_DRAW_DEBUG
}

//~ Log ----------------------------------------------------------------------------------------------------

void USpawnBudgetSubsystem::LogStats() const
{
	UE_LOG(LogSpawnBudget, Display, TEXT("SpawnBudget:"));
	UE_LOG(LogSpawnBudget, Display, TEXT("  Enabled          %s"), bEnabled ? TEXT("yes") : TEXT("no"));
	UE_LOG(LogSpawnBudget, Display, TEXT("  Population       %d / %d"), Stats.Population, Stats.MaxPopulation);
	UE_LOG(LogSpawnBudget, Display, TEXT("  This frame       %d spawned, %d despawned (budget %d / %d / %.2f ms)"),
		Stats.SpawnsThisFrame, Stats.DespawnsThisFrame,
		Limits.MaxSpawnsPerFrame, Limits.MaxDespawnsPerFrame, Limits.MaxMillisecondsPerFrame);
	UE_LOG(LogSpawnBudget, Display, TEXT("  Queue            %d waiting, %d leaving%s"),
		Stats.SpawnQueueLength, Stats.DespawnQueueLength, Stats.bTimeSliceHit ? TEXT(" (cut on the time budget)") : TEXT(""));
	UE_LOG(LogSpawnBudget, Display, TEXT("  Milliseconds     %.3f service, %.3f whole tick"),
		Stats.ServiceMilliseconds, Stats.TickMilliseconds);
	UE_LOG(LogSpawnBudget, Display, TEXT("  Pool             %s, %d hits, %d new allocations, %d parked"),
		bPoolingEnabled ? TEXT("on") : TEXT("off"), Stats.PoolHits, Stats.NewAllocations, Stats.PooledActors);
	UE_LOG(LogSpawnBudget, Display, TEXT("  Lifetime         %d spawned, %d despawned, %d refused, %d flushed"),
		TotalSpawned, TotalDespawned, RefusedCount, FlushedCount);
	UE_LOG(LogSpawnBudget, Display, TEXT("  Sources          %d of %d in range, rings x%.2f"),
		Stats.ActiveSources, Stats.RegisteredSources, RingScale);
}

void USpawnBudgetSubsystem::LogSources() const
{
	UE_LOG(LogSpawnBudget, Display, TEXT("SpawnBudget sources (%d):"), Sources.Num());

	for (const TWeakObjectPtr<ASpawnSource>& Weak : Sources)
	{
		const ASpawnSource* Source = Weak.Get();
		if (!IsValid(Source))
		{
			continue;
		}

		UE_LOG(LogSpawnBudget, Display, TEXT("  %-32s %4d/%-4d alive, %3d pending, %8.0f cm away, rings %.0f/%.0f/%.0f %s%s"),
			*Source->GetName(),
			Source->GetLiveCount(), Source->MaxAlive,
			Source->GetPendingCount(),
			Source->GetLastViewerDistance(),
			Source->Rings.SpawnIn, Source->Rings.KeepAlive, Source->Rings.Despawn,
			Source->IsInRange() ? TEXT("[in range] ") : TEXT(""),
			Source->bSourceEnabled ? TEXT("") : TEXT("[disabled]"));
	}
}

//~ Console commands ---------------------------------------------------------------------------------------

namespace SpawnBudgetPrivate
{
	static FAutoConsoleCommandWithWorldAndArgs CmdStats(
		TEXT("Spawn.Stats"),
		TEXT("Spawn.Stats - print the measured spawner counters to the log."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const USpawnBudgetSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogSpawnBudget, Warning, TEXT("Spawn.Stats: no SpawnBudget subsystem in this world."));
				return;
			}
			Subsystem->LogStats();
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdBudget(
		TEXT("Spawn.Budget"),
		TEXT("Spawn.Budget <Spawns> [Despawns] [Milliseconds] - set what one frame may cost. No arguments prints it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			USpawnBudgetSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogSpawnBudget, Warning, TEXT("Spawn.Budget: no SpawnBudget subsystem in this world."));
				return;
			}

			if (Args.Num() == 0)
			{
				const FSpawnBudgetLimits Current = Subsystem->GetBudget();
				UE_LOG(LogSpawnBudget, Display, TEXT("Spawn.Budget: %d spawns / %d despawns / %.2f ms per frame."),
					Current.MaxSpawnsPerFrame, Current.MaxDespawnsPerFrame, Current.MaxMillisecondsPerFrame);
				return;
			}

			const int32 Spawns = FMath::Max(0, FCString::Atoi(*Args[0]));

			// One argument sets the despawn budget to twice the spawn budget, because a despawn is the
			// cheaper of the two and a world that fills at N a frame has to be able to empty at least
			// that fast. Both are settable separately for the cases where that is wrong.
			const int32 Despawns = Args.Num() > 1 ? FMath::Max(0, FCString::Atoi(*Args[1])) : Spawns * 2;
			const float Milliseconds = Args.Num() > 2 ? FCString::Atof(*Args[2]) : -1.0f;

			Subsystem->SetBudget(Spawns, Despawns, Milliseconds);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdPopulation(
		TEXT("Spawn.Population"),
		TEXT("Spawn.Population <Max> - set the world population ceiling. Lowering it despawns the excess, furthest first."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			USpawnBudgetSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogSpawnBudget, Warning, TEXT("Spawn.Population: no SpawnBudget subsystem in this world."));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogSpawnBudget, Display, TEXT("Spawn.Population: %d alive, ceiling %d."),
					Subsystem->GetPopulation(), Subsystem->GetMaxPopulation());
				return;
			}

			Subsystem->SetMaxPopulation(FCString::Atoi(*Args[0]));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdClear(
		TEXT("Spawn.Clear"),
		TEXT("Spawn.Clear - despawn everything, empty both queues and destroy every parked actor."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			USpawnBudgetSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogSpawnBudget, Warning, TEXT("Spawn.Clear: no SpawnBudget subsystem in this world."));
				return;
			}
			Subsystem->ClearAll();
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdStress(
		TEXT("Spawn.Stress"),
		TEXT("Spawn.Stress <Count> - throw Count extra requests at the queue, spread over every source."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			USpawnBudgetSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogSpawnBudget, Warning, TEXT("Spawn.Stress: no SpawnBudget subsystem in this world."));
				return;
			}

			const int32 Count = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1000;
			Subsystem->RequestStress(Count);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdPool(
		TEXT("Spawn.Pool"),
		TEXT("Spawn.Pool [0|1] - park despawned actors and reuse them, or destroy them. No argument toggles."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			USpawnBudgetSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogSpawnBudget, Warning, TEXT("Spawn.Pool: no SpawnBudget subsystem in this world."));
				return;
			}

			const bool bEnable = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : !Subsystem->IsPoolingEnabled();
			Subsystem->SetPoolingEnabled(bEnable);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdRings(
		TEXT("Spawn.Rings"),
		TEXT("Spawn.Rings <Scale> - multiply every source's rings by Scale, without touching a single source."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			USpawnBudgetSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogSpawnBudget, Warning, TEXT("Spawn.Rings: no SpawnBudget subsystem in this world."));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogSpawnBudget, Display, TEXT("Spawn.Rings: x%.2f."), Subsystem->GetRingScale());
				return;
			}

			Subsystem->SetRingScale(FCString::Atof(*Args[0]));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdSources(
		TEXT("Spawn.Sources"),
		TEXT("Spawn.Sources - print every spawn source with its distance, live count and rings."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const USpawnBudgetSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogSpawnBudget, Warning, TEXT("Spawn.Sources: no SpawnBudget subsystem in this world."));
				return;
			}
			Subsystem->LogSources();
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdFlush(
		TEXT("Spawn.Flush"),
		TEXT("Spawn.Flush - throw the waiting queue away. Nothing already alive is touched."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			USpawnBudgetSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogSpawnBudget, Warning, TEXT("Spawn.Flush: no SpawnBudget subsystem in this world."));
				return;
			}
			Subsystem->FlushQueue();
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdDebug(
		TEXT("Spawn.Debug"),
		TEXT("Spawn.Debug [0|1] - draw every source's three rings and the queued spawn points."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			USpawnBudgetSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogSpawnBudget, Warning, TEXT("Spawn.Debug: no SpawnBudget subsystem in this world."));
				return;
			}

			const bool bEnable = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : !Subsystem->IsDebugDrawEnabled();
			Subsystem->SetDebugDraw(bEnable);
			UE_LOG(LogSpawnBudget, Display, TEXT("Spawn.Debug: %s"), bEnable ? TEXT("on") : TEXT("off"));
		}));
}
