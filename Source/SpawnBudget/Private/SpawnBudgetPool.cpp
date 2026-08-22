// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "SpawnBudgetPool.h"

#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "SpawnBudgetLog.h"
#include "SpawnBudgetPoolable.h"

void USpawnBudgetPool::Initialise(TSubclassOf<AActor> InActorClass, int32 InMaxPooled, bool bInParkPooledActors, const FVector& InParkLocation)
{
	ActorClass = InActorClass;
	ApplyLimits(InMaxPooled, bInParkPooledActors, InParkLocation);
}

void USpawnBudgetPool::ApplyLimits(int32 InMaxPooled, bool bInParkPooledActors, const FVector& InParkLocation)
{
	MaxPooled = FMath::Max(0, InMaxPooled);
	bParkPooledActors = bInParkPooledActors;
	ParkLocation = InParkLocation;
}

AActor* USpawnBudgetPool::Acquire(UWorld* World, const FTransform& Transform, AActor* Owner, ASpawnSource* Source, bool& bOutPoolHit)
{
	bOutPoolHit = false;

	if (!World || !ActorClass)
	{
		return nullptr;
	}

	// Take from the back so the most recently parked actor comes back first. Its components are the ones
	// most likely to still be warm - streamed textures resident, physics state cached - and popping from
	// the back is the only removal a TArray does for free.
	while (Parked.Num() > 0)
	{
		FSpawnBudgetParkedActor Entry = Parked.Pop(EAllowShrinking::No);
		if (!IsValid(Entry.Actor))
		{
			// Something destroyed it while it was parked - a level unload, a Blueprint being helpful. Not
			// an error, and not something to reuse.
			continue;
		}

		Reactivate(Entry, Transform);
		++PoolHits;
		bOutPoolHit = true;

		if (Entry.Actor->Implements<USpawnBudgetPoolable>())
		{
			ISpawnBudgetPoolable::Execute_OnSpawnedFromPool(Entry.Actor, Source, true);
		}

		return Entry.Actor;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	AActor* Spawned = World->SpawnActor<AActor>(ActorClass, Transform, SpawnParams);
	if (!Spawned)
	{
		return nullptr;
	}

	++NewAllocations;

	// A brand new actor gets the same call a reused one does, with bWasPooled false. One reset path that
	// is always exercised beats two that are supposed to agree.
	if (Spawned->Implements<USpawnBudgetPoolable>())
	{
		ISpawnBudgetPoolable::Execute_OnSpawnedFromPool(Spawned, Source, false);
	}

	return Spawned;
}

bool USpawnBudgetPool::Release(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return false;
	}

	if (Parked.Num() >= MaxPooled)
	{
		++OverflowDestroyed;
		return false;
	}

	// Told first, while it is still whole. An actor asked to stop a sound after it has been hidden and
	// unticked can no longer reliably do so.
	if (Actor->Implements<USpawnBudgetPoolable>())
	{
		ISpawnBudgetPoolable::Execute_OnReturnedToPool(Actor);
	}

	FSpawnBudgetParkedActor Entry;
	Entry.Actor = Actor;
	Deactivate(Actor, Entry);
	Parked.Add(MoveTemp(Entry));

	return true;
}

void USpawnBudgetPool::DestroyParked()
{
	for (FSpawnBudgetParkedActor& Entry : Parked)
	{
		if (IsValid(Entry.Actor))
		{
			Entry.Actor->Destroy();
		}
	}
	Parked.Reset();
}

void USpawnBudgetPool::Deactivate(AActor* Actor, FSpawnBudgetParkedActor& OutEntry) const
{
	// Every component that is ticking is switched off and written down. This is the part a pool has to get
	// right: an actor whose own tick is off but whose movement component is still running costs what it
	// always cost, and a pool that leaks ticks is slower than no pool at all. Writing them down rather
	// than switching them all back on later means an actor that had a component deliberately silenced
	// before it was despawned comes back silenced.
	OutEntry.ReEnableTickComponents.Reset();
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (Component && Component->IsComponentTickEnabled())
		{
			OutEntry.ReEnableTickComponents.Add(Component);
			Component->SetComponentTickEnabled(false);
		}
	}

	Actor->SetActorTickEnabled(false);
	Actor->SetActorEnableCollision(false);
	Actor->SetActorHiddenInGame(true);

	if (bParkPooledActors)
	{
		Actor->SetActorLocation(ParkLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void USpawnBudgetPool::Reactivate(const FSpawnBudgetParkedActor& Entry, const FTransform& Transform) const
{
	AActor* Actor = Entry.Actor;

	// Placed before it is shown, so it is never visible for a frame at the parking spot.
	Actor->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	Actor->SetActorHiddenInGame(false);
	Actor->SetActorEnableCollision(true);
	Actor->SetActorTickEnabled(true);

	for (UActorComponent* Component : Entry.ReEnableTickComponents)
	{
		if (IsValid(Component))
		{
			Component->SetComponentTickEnabled(true);
		}
	}
}
