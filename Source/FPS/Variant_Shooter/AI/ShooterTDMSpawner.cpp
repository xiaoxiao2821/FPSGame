// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterTDMSpawner.h"
#include "ShooterNPC.h"
#include "ShooterPlayerController.h"
#include "ShooterTDMGameMode.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AShooterTDMSpawner::AShooterTDMSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AShooterTDMSpawner::BeginPlay()
{
	Super::BeginPlay();

	// Only the TDM game mode uses this spawner.
	if (!GetWorld() || !GetWorld()->GetAuthGameMode()->IsA<AShooterTDMGameMode>())
	{
		return;
	}

	// Fill each team up to TeamSize with AI bots.
	for (uint8 Team = 0; Team < static_cast<uint8>(TeamSpawnTags.Num()); ++Team)
	{
		const int32 Humans = CountHumanPlayers(Team);
		const int32 Needed = FMath::Max(0, TeamSize - Humans);
		for (int32 i = 0; i < Needed; ++i)
		{
			SpawnBot(Team);
		}
	}
}

void AShooterTDMSpawner::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	TrackedBots.Reset();
}

void AShooterTDMSpawner::SpawnBot(uint8 Team)
{
	if (!IsValid(BotClass))
	{
		return;
	}

	// Pick a random PlayerStart tagged for this team.
	TArray<AActor*> PlayerStarts;
	if (TeamSpawnTags.IsValidIndex(Team))
	{
		UGameplayStatics::GetAllActorsOfClassWithTag(
			GetWorld(), APlayerStart::StaticClass(), TeamSpawnTags[Team], PlayerStarts);
	}

	if (PlayerStarts.Num() == 0)
	{
		return;
	}

	AActor* Start = PlayerStarts[FMath::RandRange(0, PlayerStarts.Num() - 1)];

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	if (AShooterNPC* Bot = GetWorld()->SpawnActor<AShooterNPC>(BotClass, Start->GetActorTransform(), SpawnParams))
	{
		Bot->SetTeam(Team);

		// Track it and respawn on death to keep the roster full.
		TrackedBots.Add(Bot);
		Bot->OnPawnDeath.AddDynamic(this, &AShooterTDMSpawner::OnBotDied);

		UE_LOG(LogTemp, Verbose, TEXT("TDM Spawner: spawned bot for team %u"), Team);
	}
}

void AShooterTDMSpawner::OnBotDied()
{
	// A bot died: make sure every team is back at its desired size.
	for (uint8 Team = 0; Team < static_cast<uint8>(TeamSpawnTags.Num()); ++Team)
	{
		const int32 Humans = CountHumanPlayers(Team);
		const int32 Alive = CountAliveBots(Team);
		const int32 ToSpawn = FMath::Max(0, TeamSize - Humans) - Alive;

		// Defer the respawn by RespawnDelay so we don't spawn mid-death-ragdoll.
		for (int32 i = 0; i < ToSpawn; ++i)
		{
			FTimerHandle Ignored;
			const uint8 TeamCopy = Team;
			GetWorld()->GetTimerManager().SetTimer(
				Ignored,
				[this, TeamCopy]() { SpawnBot(TeamCopy); },
				RespawnDelay, false);
		}
	}
}

int32 AShooterTDMSpawner::CountHumanPlayers(uint8 Team) const
{
	int32 Count = 0;
	if (!GetWorld())
	{
		return Count;
	}

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const AShooterPlayerController* PC = Cast<AShooterPlayerController>(It->Get()))
		{
			if (PC->GetTeam() == Team)
			{
				++Count;
			}
		}
	}
	return Count;
}

int32 AShooterTDMSpawner::CountAliveBots(uint8 Team) const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AShooterNPC>& Bot : TrackedBots)
	{
		if (Bot.IsValid() && !Bot->IsDead() && Bot->GetTeamByte() == Team)
		{
			++Count;
		}
	}
	return Count;
}
