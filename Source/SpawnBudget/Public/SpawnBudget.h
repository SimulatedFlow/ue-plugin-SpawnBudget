// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Runtime module for SpawnBudget. Loads at PreDefault so the world subsystem, the spawn source class and
 * the Spawn.* console commands all exist before the first game world is created - a source placed in a
 * map that is loaded on startup has to be able to fill its rings on the very first frame.
 */
class FSpawnBudgetModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
