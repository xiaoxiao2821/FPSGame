// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShooterLoginPlayerController.generated.h"

class UUserWidget;

/**
 *  PlayerController for the login scene (Lvl_Login).
 *
 *  The login scene is a CLIENT-side entry point: the start menu is created here
 *  (on the local controller), NOT in the GameMode — GameMode::BeginPlay only runs
 *  on the server process, so a network PIE/DS client would never see the menu.
 *  Works for both standalone (single-process) and network PIE.
 *
 *  Assign this class in AShooterLoginGameMode's constructor (PlayerControllerClass).
 */
UCLASS()
class FPS_API AShooterLoginPlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	/** Constructor (defaults the login menu class to BP_LoginMenu). */
	AShooterLoginPlayerController();

protected:

	/** Login menu widget class (defaults to /Game/Variant_Shooter/Blueprints/Login/BP_LoginMenu). */
	UPROPERTY(EditAnywhere, Category = "Login|UI")
	TSubclassOf<UUserWidget> LoginMenuClass;

	/** Creates and shows the login menu on the LOCAL controller, switches to UI
	 *  input mode so the player can click "start" and type a server address. */
	virtual void BeginPlay() override;

	/** Currently displayed login menu widget. */
	UPROPERTY()
	TObjectPtr<UUserWidget> LoginMenuWidget;
};
