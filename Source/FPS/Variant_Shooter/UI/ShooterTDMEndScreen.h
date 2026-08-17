// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterTDMEndScreen.generated.h"

class UTextBlock;

/**
 *  End-of-match settlement screen (TDM).
 *
 *  Pure designer-owned widget: layout AND the return-button wiring are authored
 *  in the Blueprint child. The button's OnClicked should call
 *  AShooterTDMGameMode::ServerReturnToMainMenu() (DS-safe server RPC).
 *  Populate fills the result/score texts (BlueprintNativeEvent — override to
 *  customize, or name TextBlocks "ResultText"/"ScoreText" to use the C++ fill).
 */
UCLASS()
class FPS_API UShooterTDMEndScreen : public UUserWidget
{
	GENERATED_BODY()

protected:

	/** Result text (e.g. "RED 获胜!") — optional; filled by Populate when named. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultText;

	/** Score text (e.g. "RED 12 - 30 BLUE") — optional; filled by Populate when named. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ScoreText;

public:

	/** Fills the result / score texts. Override in Blueprint to customize. */
	UFUNCTION(BlueprintNativeEvent, Category = "TDM End")
	void Populate(uint8 WinningTeam, int32 RedScore, int32 BlueScore);
	virtual void Populate_Implementation(uint8 WinningTeam, int32 RedScore, int32 BlueScore);
};
