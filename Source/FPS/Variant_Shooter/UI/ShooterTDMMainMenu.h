// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterTDMMainMenu.generated.h"

/**
 *  Pre-match main menu (TDM start screen).
 *
 *  Pure designer-owned widget: layout AND the start-button wiring are authored
 *  in the Blueprint child. The button's OnClicked should call
 *  AShooterTDMGameMode::ServerStartMatch() (DS-safe server RPC).
 */
UCLASS()
class FPS_API UShooterTDMMainMenu : public UUserWidget
{
	GENERATED_BODY()

protected:

	virtual void NativeOnInitialized() override;

public:

	/** Optional Blueprint hook fired once the menu is initialized. */
	UFUNCTION(BlueprintImplementableEvent, Category = "TDM Menu")
	void BP_OnMenuReady();

	/** Blueprint-callable team selection (0 = RED, 1 = BLUE). Wire the menu's
	 *  team buttons to this — it resolves the owning PlayerController and
	 *  issues the ServerChooseTeam RPC, so the Blueprint does not need to
	 *  chase the PC reference itself. */
	UFUNCTION(BlueprintCallable, Category = "TDM Menu")
	void SelectTeam(uint8 Team);
};
