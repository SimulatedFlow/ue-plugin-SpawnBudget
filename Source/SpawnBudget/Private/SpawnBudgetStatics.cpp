// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "SpawnBudgetStatics.h"

#include "SpawnBudgetSubsystem.h"

bool USpawnBudgetStatics::RequestSpawn(const UObject* WorldContextObject, TSubclassOf<AActor> ActorClass, const FTransform& Transform, float Weight)
{
	USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->RequestSpawn(ActorClass, Transform, Weight) : false;
}

void USpawnBudgetStatics::ReleaseActor(const UObject* WorldContextObject, AActor* Actor)
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject))
	{
		Subsystem->ReleaseActor(Actor);
	}
}

int32 USpawnBudgetStatics::RequestStress(const UObject* WorldContextObject, int32 Count)
{
	USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->RequestStress(Count) : 0;
}

void USpawnBudgetStatics::FlushQueue(const UObject* WorldContextObject)
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject))
	{
		Subsystem->FlushQueue();
	}
}

void USpawnBudgetStatics::ClearAll(const UObject* WorldContextObject)
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject))
	{
		Subsystem->ClearAll();
	}
}

void USpawnBudgetStatics::SetBudget(const UObject* WorldContextObject, int32 MaxSpawnsPerFrame, int32 MaxDespawnsPerFrame, float MaxMillisecondsPerFrame)
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetBudget(MaxSpawnsPerFrame, MaxDespawnsPerFrame, MaxMillisecondsPerFrame);
	}
}

FSpawnBudgetLimits USpawnBudgetStatics::GetBudget(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetBudget() : FSpawnBudgetLimits();
}

void USpawnBudgetStatics::SetMaxPopulation(const UObject* WorldContextObject, int32 MaxPopulation)
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetMaxPopulation(MaxPopulation);
	}
}

int32 USpawnBudgetStatics::GetMaxPopulation(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetMaxPopulation() : 0;
}

int32 USpawnBudgetStatics::GetPopulation(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetPopulation() : 0;
}

int32 USpawnBudgetStatics::GetQueueLength(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetQueueLength() : 0;
}

int32 USpawnBudgetStatics::GetSpawnQueueLength(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetSpawnQueueLength() : 0;
}

int32 USpawnBudgetStatics::GetDespawnQueueLength(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetDespawnQueueLength() : 0;
}

void USpawnBudgetStatics::SetPoolingEnabled(const UObject* WorldContextObject, bool bEnabled)
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetPoolingEnabled(bEnabled);
	}
}

bool USpawnBudgetStatics::IsPoolingEnabled(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->IsPoolingEnabled() : false;
}

void USpawnBudgetStatics::SetRingScale(const UObject* WorldContextObject, float RingScale)
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetRingScale(RingScale);
	}
}

float USpawnBudgetStatics::GetRingScale(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetRingScale() : 1.0f;
}

void USpawnBudgetStatics::SetSpawningEnabled(const UObject* WorldContextObject, bool bEnabled)
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetEnabled(bEnabled);
	}
}

bool USpawnBudgetStatics::IsSpawningEnabled(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->IsEnabled() : false;
}

void USpawnBudgetStatics::SetDebugDraw(const UObject* WorldContextObject, bool bEnabled)
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetDebugDraw(bEnabled);
	}
}

FSpawnBudgetStats USpawnBudgetStatics::GetStats(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetStats() : FSpawnBudgetStats();
}

int32 USpawnBudgetStatics::GetPoolHits(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetPoolHits() : 0;
}

int32 USpawnBudgetStatics::GetNewAllocations(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetNewAllocations() : 0;
}

int32 USpawnBudgetStatics::GetRefusedCount(const UObject* WorldContextObject)
{
	const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetRefusedCount() : 0;
}

void USpawnBudgetStatics::GetSources(const UObject* WorldContextObject, TArray<ASpawnSource*>& OutSources)
{
	OutSources.Reset();

	if (const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject))
	{
		Subsystem->GetSources(OutSources);
	}
}

void USpawnBudgetStatics::DespawnSource(const UObject* WorldContextObject, ASpawnSource* Source)
{
	if (USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(WorldContextObject))
	{
		Subsystem->DespawnSource(Source);
	}
}
