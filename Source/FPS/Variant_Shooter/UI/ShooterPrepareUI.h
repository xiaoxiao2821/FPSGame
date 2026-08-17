// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterPrepareUI.generated.h"

/**
 *  Base class for the prepare-phase countdown widget (created by the Player
 *  Controller, driven by the replicated countdown events).
 *
 *  The Blueprint child only implements the two events below (update the
 *  countdown text / hide the widget); the PlayerController shows/hides the
 *  widget and forwards the seconds.
 */
UCLASS()
class FPS_API UShooterPrepareUI : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Blueprint hook: update the countdown display (called every second). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|HUD", meta = (DisplayName = "OnPrepareCountdown"))
	void BP_OnPrepareCountdown(int32 SecondsRemaining);

	/** Blueprint hook: prepare phase ended — hide/clear the countdown. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|HUD", meta = (DisplayName = "OnPrepareEnded"))
	void BP_OnPrepareEnded();
};
