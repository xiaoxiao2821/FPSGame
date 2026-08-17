// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShooterGameMode.generated.h"

class UShooterUI;

/**
 *  Simple GameMode for a first person shooter game
 *  Manages game UI
 *  Keeps track of team scores
 */
UCLASS(abstract)
class FPS_API AShooterGameMode : public AGameModeBase
{
	GENERATED_BODY()
	
protected:

	/** Pointer to the UI widget */
	TObjectPtr<UShooterUI> ShooterUI;

	/** Map of scores by team ID */
	TMap<uint8, int32> TeamScores;

public:

	/** Type of UI widget to spawn */
	UPROPERTY(EditAnywhere, Category="Shooter")
	TSubclassOf<UShooterUI> ShooterUIClass;

	/** Determines how many local players should be spawned on game start */
	UPROPERTY(EditDefaultsOnly, Category="Local Multiplayer", meta = (ClampMin = 1, ClampMax = 4))
	int32 NumberOfLocalPlayers = 1;

	/** Used to assign players to different PlayerStarts in the level */
	int32 CurrentPlayerStartAssignment = 0;

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Assigns a PlayerStart to a specific player */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

public:

	/** Increases the score for the given team */
	void IncrementTeamScore(uint8 TeamByte);

	/**
	 * Called when a character is killed. KillerTeam is the team that should be
	 * awarded the point (NOT the victim's team). Derived modes (e.g. TDM) override
	 * this to add win conditions and to prevent team-kill / suicide scoring.
	 */
	virtual void ReportKill(uint8 KillerTeam);

	/** Returns the current score for the given team (0 if never scored) */
	int32 GetTeamScore(uint8 TeamByte) const;

	/** Returns true if enemy NPCs should be used */
	virtual bool ShouldSpawnEnemyNPCs() const;
};
