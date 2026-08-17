// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterTDMSpawner.generated.h"

class AShooterNPC;
class AShooterPlayerController;

/**
 *  Team Deathmatch AI filler.
 *
 *  On match start, for each team it counts the human players already present and
 *  spawns enough AI bots (BotClass) at that team's spawn points to reach TeamSize.
 *  When a bot dies its OnPawnDeath fires and the spawner tops the team back up, so
 *  each side always stays at the configured roster size (human + AI).
 */
UCLASS()
class FPS_API AShooterTDMSpawner : public AActor
{
	GENERATED_BODY()

protected:

	/** NPC class to spawn as a bot (author a Blueprint with its weapon pre-configured). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TDM AI")
	TSubclassOf<AShooterNPC> BotClass;

	/** Desired roster size per team (humans + bots). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TDM AI", meta = (ClampMin = 1, ClampMax = 16))
	int32 TeamSize = 4;

	/** PlayerStart actor tags, one per team (index 0 / 1). Must match the level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TDM AI")
	TArray<FName> TeamSpawnTags = { FName("RED"), FName("BLUE") };

	/** Delay before a dead bot is replaced. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TDM AI", meta = (ClampMin = 0, ClampMax = 30))
	float RespawnDelay = 3.0f;

public:

	/** Constructor */
	AShooterTDMSpawner();

	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

protected:

	/** Spawns one bot on the given team and tracks it. */
	void SpawnBot(uint8 Team);

	/** Called when any tracked bot dies; tops up both teams. */
	UFUNCTION()
	void OnBotDied();

	/** Counts human player controllers on the given team. */
	int32 CountHumanPlayers(uint8 Team) const;

	/** Counts still-alive tracked bots on the given team. */
	int32 CountAliveBots(uint8 Team) const;

	/** Bots we have spawned, so we can count the living. */
	UPROPERTY()
	TArray<TWeakObjectPtr<AShooterNPC>> TrackedBots;
};
