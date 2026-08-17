// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/UI/ShooterTDMEndScreen.h"
#include "Components/TextBlock.h"

void UShooterTDMEndScreen::Populate_Implementation(uint8 WinningTeam, int32 RedScore, int32 BlueScore)
{
	const FString TeamName = (WinningTeam == 0) ? TEXT("RED") : TEXT("BLUE");
	const FText Result = FText::FromString(FString::Printf(TEXT("%s 获胜!"), *TeamName));
	const FText Score = FText::FromString(FString::Printf(TEXT("RED %d  -  %d BLUE"), RedScore, BlueScore));

	if (ResultText)
	{
		ResultText->SetText(Result);
	}
	if (ScoreText)
	{
		ScoreText->SetText(Score);
	}
}
