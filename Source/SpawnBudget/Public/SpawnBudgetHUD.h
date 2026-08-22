// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SpawnBudgetHUD.generated.h"

class UCanvas;

/**
 * The statistics box for SpawnBudget, drawn on UCanvas from AHUD.
 *
 * Canvas rather than UMG, and there are deliberately no buttons on it. Two reasons, pulling the same way:
 *
 *   - This box has to survive a cooked Shipping build. DrawDebug is compiled out there and a debug widget
 *     is usually stripped; a Canvas overlay is not. It is a real overlay, not a development one - which
 *     matters for a plugin whose entire claim is a number, because a number that disappears in the build
 *     that ships is a number nobody can check.
 *
 *   - Anything that has to be *clicked* belongs in UMG instead. An AHUD hit box is tested against
 *     UGameViewportClient::GetMousePosition(), which reports nothing at all on a machine with no mouse
 *     attached - a capture rig, a build agent, a headless test - so the click never lands. Widgets are
 *     not affected. The numbers live here, where they cost nothing and always draw; controls live in a
 *     widget, where they always receive the click.
 *
 * A project that already has a HUD class does not have to reparent it: turn on bAutoDrawStatsOnAnyHUD in
 * Project Settings and the same box is drawn through AHUD::OnHUDPostRender instead. The two paths know
 * about each other and cannot stack.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Spawn Budget HUD"))
class SPAWNBUDGET_API ASpawnBudgetHUD : public AHUD
{
	GENERATED_BODY()

public:
	ASpawnBudgetHUD();

	//~ AActor interface
	virtual void BeginPlay() override;

	//~ AHUD interface
	virtual void DrawHUD() override;

	/** Draw the statistics box. Toggled from Blueprint, or from a widget button. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|HUD")
	bool bShowStats = true;

	/** Top-left corner of the statistics box, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|HUD")
	FVector2D StatsBoxOrigin = FVector2D(28.0f, 90.0f);

	/** Width of the statistics box, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBudget|HUD", meta = (ClampMin = "220.0"))
	float StatsBoxWidth = 460.0f;

	/** Flip the statistics box on and off. This is what a STATS button in a widget calls. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBudget|HUD")
	void ToggleStats();
};
