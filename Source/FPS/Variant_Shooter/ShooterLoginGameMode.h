// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShooterLoginGameMode.generated.h"

class UUserWidget;

/**
 *  GameMode for the login scene (Lvl_Login).
 *
 *  The login scene is a CLIENT-side entry point: the start menu UI is created
 *  by AShooterLoginPlayerController::BeginPlay() on the LOCAL controller (see
 *  ShooterLoginPlayerController). This GameMode only configures which classes
 *  the level uses — it does NOT create UI (GameMode::BeginPlay runs on the
 *  server only, so UI created here would never appear on a network client).
 *
 *  Set this class (or a Blueprint subclass) as the GameMode Override of the
 *  login level in World Settings.
 */
UCLASS()
class FPS_API AShooterLoginGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	/** Constructor (assigns the login PlayerController class). */
	AShooterLoginGameMode();
};
