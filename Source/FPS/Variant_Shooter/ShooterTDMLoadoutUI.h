// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterWeapon.h"
#include "ShooterCharacter.h"
#include "ShooterTDMLoadoutUI.generated.h"

/**
 *  Respawn loadout panel (TDM weapon pick).
 *
 *  The visual layout is authored in a Blueprint child of this class. The C++ side
 *  drives it: the player controller calls BP_Populate (with the available weapons),
 *  BP_Show / BP_Hide, and the widget calls back SelectWeapon when the player clicks
 *  a weapon. The panel is *closed* automatically by the character when the player
 *  moves or fires — at which point the chosen weapon is equipped.
 */
UCLASS()
class FPS_API UShooterTDMLoadoutUI : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Sets the character this panel controls (called by the player controller). */
	UFUNCTION(BlueprintCallable, Category = "Loadout")
	void SetOwningCharacter(AShooterCharacter* Character);

	/** Called by the player controller to fill the panel with selectable weapons. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loadout")
	void BP_Populate(const TArray<TSubclassOf<AShooterWeapon>>& Weapons);

	/** Called by the player controller to show the panel. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loadout")
	void BP_Show();

	/** Called by the player controller (or character) to hide the panel. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Loadout")
	void BP_Hide();

	/** Called by the widget when the player clicks a weapon. Stores it as the pending pick. */
	UFUNCTION(BlueprintCallable, Category = "Loadout")
	void SelectWeapon(TSubclassOf<AShooterWeapon> Weapon);

protected:

	/** Character whose loadout this panel edits. Weak ref so we never dangle. */
	TWeakObjectPtr<AShooterCharacter> OwningCharacter;
};
