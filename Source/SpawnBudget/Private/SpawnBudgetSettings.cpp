// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "SpawnBudgetSettings.h"

USpawnBudgetSettings::USpawnBudgetSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("SpawnBudget");

	// Wider than a source's own defaults on purpose. An actor a Blueprint asked for by name was asked for
	// deliberately, and taking it away at the same distance as one of a hundred ambient props is a
	// surprise nobody wants to debug.
	DefaultRings.SpawnIn = 6000.0f;
	DefaultRings.KeepAlive = 9000.0f;
	DefaultRings.Despawn = 12000.0f;
}

FName USpawnBudgetSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName USpawnBudgetSettings::GetSectionName() const
{
	return TEXT("SpawnBudget");
}

const USpawnBudgetSettings& USpawnBudgetSettings::Get()
{
	const USpawnBudgetSettings* Settings = GetDefault<USpawnBudgetSettings>();
	check(Settings);
	return *Settings;
}
