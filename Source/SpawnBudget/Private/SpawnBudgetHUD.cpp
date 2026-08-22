// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "SpawnBudgetHUD.h"

#include "Engine/Canvas.h"
#include "SpawnBudgetSettings.h"
#include "SpawnBudgetSubsystem.h"

ASpawnBudgetHUD::ASpawnBudgetHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ASpawnBudgetHUD::BeginPlay()
{
	Super::BeginPlay();

	bShowStats = USpawnBudgetSettings::Get().bShowStatsByDefault;
}

void ASpawnBudgetHUD::ToggleStats()
{
	bShowStats = !bShowStats;
}

void ASpawnBudgetHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !bShowStats)
	{
		return;
	}

	// Everything drawn is read from the subsystem on the frame it is drawn. Nothing is cached here, so the
	// box cannot claim one thing while the spawner does another.
	if (const USpawnBudgetSubsystem* Subsystem = USpawnBudgetSubsystem::Get(this))
	{
		Subsystem->DrawStatsBox(Canvas, StatsBoxOrigin, StatsBoxWidth);
	}
}
