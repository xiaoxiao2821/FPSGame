// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterLoginGameMode.h"
#include "ShooterLoginPlayerController.h"

AShooterLoginGameMode::AShooterLoginGameMode()
{
	// The login scene is a CLIENT-side entry point: the start menu is created by
	// the PlayerController (BeginPlay, local controller only). The GameMode only
	// assigns which controller the level uses — it must NOT create UI here, since
	// GameMode::BeginPlay runs on the server process only and would never show a
	// widget on a network client.
	PlayerControllerClass = AShooterLoginPlayerController::StaticClass();
}
