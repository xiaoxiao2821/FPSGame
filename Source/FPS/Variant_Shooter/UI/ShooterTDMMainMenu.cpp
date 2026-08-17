// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/UI/ShooterTDMMainMenu.h"
#include "ShooterPlayerController.h"

void UShooterTDMMainMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Designer hook: the Blueprint child can react to the menu being shown.
	BP_OnMenuReady();
}

void UShooterTDMMainMenu::SelectTeam(uint8 Team)
{
	// Resolve the owning PlayerController and issue the team-selection RPC
	// (server-authoritative). No-op when the menu has no local player.
	if (AShooterPlayerController* PC = Cast<AShooterPlayerController>(GetOwningPlayer()))
	{
		PC->ServerChooseTeam(Team);
	}
}
