// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "SpawnBudget.h"
#include "SpawnBudgetLog.h"

DEFINE_LOG_CATEGORY(LogSpawnBudget);

#define LOCTEXT_NAMESPACE "FSpawnBudgetModule"

void FSpawnBudgetModule::StartupModule()
{
	UE_LOG(LogSpawnBudget, Log, TEXT("SpawnBudget started."));
}

void FSpawnBudgetModule::ShutdownModule()
{
	UE_LOG(LogSpawnBudget, Log, TEXT("SpawnBudget shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSpawnBudgetModule, SpawnBudget)
