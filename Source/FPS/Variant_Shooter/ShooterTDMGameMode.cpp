// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterTDMGameMode.h"
#include "ShooterPlayerController.h"
#include "ShooterWeapon.h"
#include "Weapons/ShooterProjectile.h"
#include "ShooterUI.h"
#include "ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/UI/ShooterTDMMainMenu.h"
#include "Variant_Shooter/UI/ShooterTDMEndScreen.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"

AShooterTDMGameMode::AShooterTDMGameMode()
{
	// Default the AI filler to the project's shooter NPC (weapon pre-configured
	// to the pistol). Overridable in the Blueprint's Class Defaults.
	static ConstructorHelpers::FClassFinder<AShooterNPC> BotFinder(
		TEXT("/Game/Variant_Shooter/Blueprints/AI/BP_ShooterNPC.BP_ShooterNPC_C"));
	if (BotFinder.Succeeded())
	{
		BotClass = BotFinder.Class;
	}

	// Explicit spawn locations read from Lvl_Shooter's PlayerStarts, so spawn
	// assignment never depends on World Partition streaming. Y < 0 = RED side.
	RedSpawnLocations = {
		FTransform(FRotator(0.0f, 46.4f, 0.0f), FVector(-800.0f, -1050.0f, 92.0f)),
		FTransform(FRotator(0.0f, 117.0f, 0.0f), FVector(1150.0f, -1450.0f, 92.0f)),
		FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(-300.0f, -1350.0f, 292.0f)),
		FTransform(FRotator(0.0f, -90.0f, 0.0f), FVector(-152.4f, -691.8f, 92.1f)),
	};
	BlueSpawnLocations = {
		FTransform(FRotator(0.0f, -90.0f, 0.0f), FVector(-534.0f, 1506.3f, 92.1f)),
		FTransform(FRotator(0.0f, -90.0f, 0.0f), FVector(396.1f, 1503.3f, 92.1f)),
		FTransform(FRotator(0.0f, -90.0f, 0.0f), FVector(-983.9f, 1513.3f, 262.1f)),
		FTransform(FRotator(0.0f, -90.0f, 0.0f), FVector(1046.1f, 803.3f, 82.1f)),
	};
}

void AShooterTDMGameMode::ReportKill(uint8 KillerTeam)
{
	// Once the match is decided, stop accepting score.
	if (bMatchEnded)
	{
		return;
	}

	// Award the point to the killer's team.
	int32& Score = TeamScores.FindOrAdd(KillerTeam, 0);
	++Score;

	// DS-correct HUD update: the HUD lives on each client, so broadcast the new
	// score to every connected player (the server-side ShooterUI is null here).
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AShooterPlayerController* SPC = Cast<AShooterPlayerController>(It->Get()))
		{
			SPC->Client_UpdateScore(KillerTeam, Score);
		}
	}

	// Win check.
	if (Score >= KillTarget)
	{
		EndMatch(KillerTeam);
	}
}

AActor* AShooterTDMGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// Determine the requesting player's team.
	uint8 Team = 0;
	if (const AShooterPlayerController* PC = Cast<AShooterPlayerController>(Player))
	{
		Team = PC->GetTeam();
	}

	// Requirement: PlayerStart 1-4 = RED, 5-8 = BLUE.
	if (AActor* Start = FindTeamPlayerStart(Team))
	{
		return Start;
	}

	// Fallback to the base (tag-less) selection.
	AActor* Fallback = Super::ChoosePlayerStart_Implementation(Player);
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: ChoosePlayerStart - fallback start = %s"), *GetNameSafe(Fallback));
	return Fallback;
}

AActor* AShooterTDMGameMode::FindTeamPlayerStart(uint8 Team)
{
	// 1) Explicitly configured spawn locations (immune to World Partition
	//    streaming) — resolved to real PlayerStarts in BeginPlay.
	if (Team < 2 && ResolvedSpawnPoints[Team].Num() > 0)
	{
		const int32 Index = NextPlayerStartIndex[Team] % ResolvedSpawnPoints[Team].Num();
		++NextPlayerStartIndex[Team];
		AActor* Start = ResolvedSpawnPoints[Team][Index].Get();
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: FindTeamPlayerStart - team %u -> %s (cfg slot %d/%d)"),
			Team, *GetNameSafe(Start), Index, ResolvedSpawnPoints[Team].Num());
		return Start;
	}

	// 2) Requirement: numbered spawn points Player1..Player8 —
	//    Player1-4 belong to RED (team 0), Player5-8 to BLUE (team 1).
	TArray<AActor*> AllStarts;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), AllStarts);

	// Collect numbered starts of this team, keeping their numeric order so we
	// can hand them out sequentially (1,2,3,4 / 5,6,7,8) instead of piling up.
	struct FStartEntry
	{
		int32 Number;
		AActor* Actor;
	};
	TArray<FStartEntry> Candidates;
	for (AActor* Start : AllStarts)
	{
		// Parse the trailing number of the actor name, e.g. "Player1" -> 1.
		FString Name = Start->GetName();
		int32 i = Name.Len() - 1;
		while (i >= 0 && FChar::IsDigit(Name[i]))
		{
			--i;
		}
		const FString NumStr = Name.Mid(i + 1);
		if (NumStr.IsEmpty())
		{
			continue;
		}
		const int32 Num = FCString::Atoi(*NumStr);
		// Requirement: numbered spawns 1-4 = RED, 5-8 = BLUE. The level's actual
		// actor names are Player0/PlayerStartN, so treat 0 as RED too (0-4 RED,
		// 5-8 BLUE) to stay compatible with the current layout.
		const uint8 StartTeam = (Num >= 0 && Num <= 4) ? 0 : (Num >= 5 && Num <= 8) ? 1 : 255;
		if (StartTeam == Team)
		{
			Candidates.Add({ Num, Start });
		}
	}

	// Sort by number so "Player1" precedes "Player2" etc.
	Candidates.Sort([](const FStartEntry& A, const FStartEntry& B) { return A.Number < B.Number; });

	if (Candidates.Num() > 0)
	{
		// Round-robin: hand out the next spawn point in sequence.
		const int32 Index = NextPlayerStartIndex[Team] % Candidates.Num();
		++NextPlayerStartIndex[Team];
		AActor* Start = Candidates[Index].Actor;
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: FindTeamPlayerStart - team %u -> %s (slot %d/%d)"),
			Team, *Start->GetName(), Index, Candidates.Num());
		return Start;
	}

	// Fallback: tagged PlayerStarts (legacy layout).
	if (TeamSpawnTags.IsValidIndex(Team))
	{
		TArray<AActor*> Tagged;
		UGameplayStatics::GetAllActorsOfClassWithTag(
			GetWorld(), APlayerStart::StaticClass(), TeamSpawnTags[Team], Tagged);
		if (Tagged.Num() > 0)
		{
			const int32 Index = NextPlayerStartIndex[Team] % Tagged.Num();
			++NextPlayerStartIndex[Team];
			UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: FindTeamPlayerStart - team %u TAG fallback -> %s (slot %d/%d)"),
				Team, *Tagged[Index]->GetName(), Index, Tagged.Num());
			return Tagged[Index];
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: FindTeamPlayerStart - team %u found NO spawn point!"), Team);
	return nullptr;
}

APlayerController* AShooterTDMGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	// The base Login CREATES the PlayerController; only after that exists can
	// we assign a team. This runs BEFORE the base PostLogin -> RestartPlayer
	// (which spawns the pawn), so ChoosePlayerStart already sees the correct
	// team — this fixes "every player first-spawns on RED during prepare".
	APlayerController* NewPC = Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);

	if (AShooterPlayerController* PC = Cast<AShooterPlayerController>(NewPC))
	{
		const uint8 Team = static_cast<uint8>(NextTeamSlot % 2);
		++NextTeamSlot;

		PC->SetTeam(Team);
		PC->SetTeamTags(TeamSpawnTags);

		// A newcomer always starts frozen on the menu — each player must click
		// "start" to enter the match. (Replicated to the client PC, so this
		// works on a dedicated server too.)
		PC->SetMatchReady(false);
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);

		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: Login - %s assigned team %d (frozen)"), *PC->GetName(), Team);
	}

	return NewPC;
}

void AShooterTDMGameMode::PostLogin(APlayerController* NewPlayer)
{
	AShooterPlayerController* PC = Cast<AShooterPlayerController>(NewPlayer);
	if (!PC)
	{
		Super::PostLogin(NewPlayer);
		return;
	}

	Super::PostLogin(NewPlayer);

	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: PostLogin - %s team %d frozen (awaiting start)"), *PC->GetName(), PC->GetTeam());

	// Diagnostic + safety net: after the base flow (which restarts the player)
	// the player MUST have a pawn. The second PIE viewport has been observed to
	// join WITHOUT one (no spawn happened at login), leaving it until the first
	// respawn — force a restart here if that happens.
	if (!PC->GetPawn())
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: PostLogin - %s has NO pawn after base spawn, forcing RestartPlayer"), *PC->GetName());
		RestartPlayer(PC);
		if (!PC->GetPawn())
		{
			UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: PostLogin - %s still has NO pawn after forced restart!"), *PC->GetName());
		}
	}

	// NOTE: roster is only adjusted when a player actually ENTERS the match
	// (StartPlayerMatch), so a mid-match joiner first replaces a bot.
}

void AShooterTDMGameMode::BeginPlay()
{
	// Base creates the scoreboard HUD and spawns/possesses players. The menu and
	// input freeze are handled per-client (DS-safe): each client shows the menu
	// when it possesses a pawn while the match phase is MainMenu.
	Super::BeginPlay();

	MatchPhase = ETDMMatchPhase::MainMenu;
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: BeginPlay - phase=MainMenu, MainMenuClass=%s, EndScreenClass=%s, KillTarget=%d, TeamSize=%d"),
		*GetNameSafe(MainMenuClass), *GetNameSafe(EndScreenClass), KillTarget, TeamSize);

	// Remove the server-side HUD the base mode just created: on a dedicated
	// server it is invisible, and on a listen server it would double up with the
	// per-client HUD we spawn when the match starts.
	if (IsValid(ShooterUI))
	{
		ShooterUI->RemoveFromParent();
		ShooterUI = nullptr;
	}

	// No menu asset configured => go straight to playing (fallback behavior).
	if (!MainMenuClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: BeginPlay - MainMenuClass is NULL, starting match immediately"));
		StartMatch();
	}

	// Safety net: periodically ensure every team is up to TeamSize (bots revive
	// in place on normal death; this only replaces bots that were destroyed).
	GetWorldTimerManager().SetTimer(
		RosterSafetyTimer,
		this,
		&AShooterTDMGameMode::EnsureRosterSize,
		5.0f,
		true);

	// Diagnostic: is the navigation system up? If the RecastNavMesh did not
	// generate (Dynamic WP nav mesh), every AI MoveTo fails and bots stand
	// still — the "AI doesn't move" symptom. The level MUST keep Runtime
	// Generation = Dynamic (movable objects), so when tiles are missing we
	// force a runtime rebuild instead of switching to Static.
	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetNavigationSystem(GetWorld()))
	{
		const ANavigationData* NavData = Cast<ANavigationData>(NavSys->GetMainNavData());
		const ARecastNavMesh* RNav = Cast<ARecastNavMesh>(NavData);
		const int32 Tiles = RNav ? RNav->GetNumActiveTiles() : -1;
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: NavSystem=%s mainNavData=%s tiles=%d runtimeGen=%d"),
			*NavSys->GetName(),
			NavData ? *NavData->GetName() : TEXT("NULL"),
			Tiles,
			NavData ? static_cast<int32>(NavData->GetRuntimeGenerationMode()) : -1);

		// Dynamic nav mesh with ZERO tiles = the runtime rebuild never ran
		// (e.g. maxTiles mismatch after engine upgrade, or the rebuild was
		// gated). Force a synchronous Build() shortly after startup so the
		// base nav data actually exists and AI can path. Movable objects keep
		// the mesh dynamic — we only ensure the initial data is present.
		if (RNav && Tiles <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: NavMesh has no tiles, forcing runtime Build() in 3s..."));
			FTimerHandle NavBuildTimer;
			GetWorldTimerManager().SetTimer(NavBuildTimer, [NavSys]()
			{
				UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: NavSys->Build() starting..."));
				NavSys->Build();
				UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: NavSys->Build() done"));
			}, 3.0f, false);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: NavSystem is NULL! AI will not move."));
	}

	// Resolve the explicitly configured spawn locations into real (invisible)
	// PlayerStart actors so they are immune to World Partition streaming.
	const TArray<FTransform>* CfgSpawns[2] = { &RedSpawnLocations, &BlueSpawnLocations };
	for (uint8 Team = 0; Team < 2; ++Team)
	{
		for (const FTransform& T : *CfgSpawns[Team])
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (APlayerStart* PS = GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), T, SpawnParams))
			{
				PS->SetActorHiddenInGame(true);
				ResolvedSpawnPoints[Team].Add(PS);
			}
		}
	}
	if (ResolvedSpawnPoints[0].Num() > 0 || ResolvedSpawnPoints[1].Num() > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: BeginPlay - configured spawns resolved: RED %d, BLUE %d"),
			ResolvedSpawnPoints[0].Num(), ResolvedSpawnPoints[1].Num());
	}
}

void AShooterTDMGameMode::StartMatch()
{
	if (MatchPhase != ETDMMatchPhase::MainMenu)
	{
		return;
	}

	// Requirement: after the menu the match enters a 30s PREPARE phase
	// (no bots, no damage); bots are filled when the countdown completes.
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: StartMatch - entering prepare phase"));
	StartPreparePhase();
}

void AShooterTDMGameMode::StartPreparePhase()
{
	if (MatchPhase != ETDMMatchPhase::MainMenu)
	{
		return;
	}

	MatchPhase = ETDMMatchPhase::Prepare;
	PrepareRemainingSeconds = FMath::Max(1, FMath::CeilToInt(PrepareTime));
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: Prepare phase started (%d s) - no bots, damage immune"),
		PrepareRemainingSeconds);

	// Initial broadcast (UI shows the full countdown), then tick every second.
	BroadcastPrepareCountdown();
	GetWorldTimerManager().SetTimer(PrepareTimer, this, &AShooterTDMGameMode::TickPrepareCountdown, 1.0f, true);
}

void AShooterTDMGameMode::TickPrepareCountdown()
{
	--PrepareRemainingSeconds;
	if (PrepareRemainingSeconds <= 0)
	{
		CompletePreparePhase();
		return;
	}
	BroadcastPrepareCountdown();
}

void AShooterTDMGameMode::CompletePreparePhase()
{
	GetWorldTimerManager().ClearTimer(PrepareTimer);
	PrepareRemainingSeconds = 0;

	MatchPhase = ETDMMatchPhase::Playing;
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: Prepare phase ended - phase=Playing, resetting score, respawning players, spawning bots"));

	// Requirement: reset the scoreboard for the real match (be explicit and
	// push the reset to every client's HUD).
	TeamScores.Reset();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AShooterPlayerController* SPC = Cast<AShooterPlayerController>(It->Get()))
		{
			SPC->Client_UpdateScore(0, 0);
			SPC->Client_UpdateScore(1, 0);
		}
	}

	// Requirement: players who moved around during the prepare phase go back
	// through the NORMAL respawn flow — destroying the pawn triggers
	// OnPawnDestroyed, which server-authoritatively respawns it at the team's
	// spawn point with full HP, spawn protection and a fresh weapon.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AShooterPlayerController* SPC = Cast<AShooterPlayerController>(It->Get()))
		{
			if (APawn* Pawn = SPC->GetPawn())
			{
				Pawn->Destroy();
			}
		}
	}

	// Requirement: bots are only spawned AFTER the countdown (not during).
	FillTeamRoster();
	BP_OnMatchStarted();

	// Tell every client the prepare UI is done (hide countdown).
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AShooterPlayerController* SPC = Cast<AShooterPlayerController>(It->Get()))
		{
			SPC->Client_OnPrepareEnded();
		}
	}
}

void AShooterTDMGameMode::BroadcastPrepareCountdown()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AShooterPlayerController* SPC = Cast<AShooterPlayerController>(It->Get()))
		{
			SPC->Client_OnPrepareCountdown(PrepareRemainingSeconds);
		}
	}
}

void AShooterTDMGameMode::StartPlayerMatch(AShooterPlayerController* PC)
{
	if (!PC || bMatchEnded)
	{
		return;
	}
	if (PC->IsMatchReady())
	{
		return; // already in the match
	}

	// Mark THIS player ready, unfreeze them and tell their client to enter.
	PC->SetMatchReady(true);
	PC->SetIgnoreMoveInput(false);
	PC->SetIgnoreLookInput(false);
	PC->Client_OnMatchStarted(ShooterUIClass);

	// Equip their weapon (default gun if no loadout panel is configured).
	if (AShooterCharacter* Char = Cast<AShooterCharacter>(PC->GetPawn()))
	{
		PC->OpenLoadoutUI(Char);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: StartPlayerMatch - %s has no pawn yet, weapon on next spawn"), *PC->GetName());
	}

	// The FIRST entrant starts the 30s PREPARE phase. No bots spawn during it
	// (requirement) — FillTeamRoster runs when the countdown completes. Players
	// entering during the prepare phase just unfreeze above; they can move but
	// deal no damage until the countdown ends.
	if (MatchPhase == ETDMMatchPhase::MainMenu)
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: StartPlayerMatch - first entrant %s starts prepare phase"), *PC->GetName());
		StartPreparePhase();
	}

	// During the prepare phase there are no bots yet, so nothing to replace.
	if (MatchPhase == ETDMMatchPhase::Playing)
	{
		// This player is now in: take a live bot's slot on their team so the
		// side stays at TeamSize (human + AI).
		ReplaceBotWithPlayer(PC);
	}
}

void AShooterTDMGameMode::ReturnToMainMenu()
{
	// First-pass: a clean level restart re-runs BeginPlay and shows the menu again.
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		PC->ConsoleCommand(TEXT("RestartLevel"));
	}
}

void AShooterTDMGameMode::EndMatch(uint8 WinningTeam)
{
	if (bMatchEnded)
	{
		return;
	}

	bMatchEnded = true;

	UE_LOG(LogTemp, Warning, TEXT("TDM: Team %u reached %d kills - match over."), WinningTeam, KillTarget);

	MatchPhase = ETDMMatchPhase::Ended;

	// Show the settlement screen on every client (client-side widget).
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AShooterPlayerController* SPC = Cast<AShooterPlayerController>(It->Get()))
		{
			SPC->Client_ShowEndScreen(WinningTeam, GetTeamScore(0), GetTeamScore(1), EndScreenClass);
		}
	}

	// Let the Blueprint layer (HUD / end screen) react.
	BP_OnMatchEnded(WinningTeam);
}

//~ AI filler ---------------------------------------------------------------

void AShooterTDMGameMode::FillTeamRoster()
{
	if (bMatchEnded || !IsValid(BotClass))
	{
		return;
	}

	// REQUIREMENT: every team ends up at EXACTLY TeamSize total units. Humans
	// already present (e.g. players who entered during the prepare phase) take
	// roster slots first, so bots are only spawned for the shortfall — this
	// prevents a team from ever reaching TeamSize + humans.
	for (uint8 Team = 0; Team < static_cast<uint8>(TeamSpawnTags.Num()); ++Team)
	{
		const int32 Humans = CountHumanPlayers(Team);
		const int32 Alive = CountAliveBots(Team);
		const int32 Needed = FMath::Max(0, TeamSize - Humans - Alive);
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: FillTeamRoster - team %u humans=%d aliveBots=%d -> spawning %d"),
			Team, Humans, Alive, Needed);
		for (int32 i = 0; i < Needed; ++i)
		{
			SpawnBot(Team);
		}
	}
}

void AShooterTDMGameMode::ReplaceBotWithPlayer(AShooterPlayerController* PC)
{
	if (!PC)
	{
		return;
	}

	// The human takes a live bot's slot on their team. If the team has no live
	// bot to evict (all dead / never filled), the human simply occupies the slot.
	if (RemoveRandomBot(PC->GetTeam()))
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: ReplaceBotWithPlayer - %s team %d evicted a bot"), *PC->GetName(), PC->GetTeam());
	}
}

void AShooterTDMGameMode::SpawnBot(uint8 Team)
{
	if (!IsValid(BotClass) || bMatchEnded)
	{
		return;
	}

	// Pick a PlayerStart for this team (PlayerStart 1-4 = RED, 5-8 = BLUE).
	AActor* Start = FindTeamPlayerStart(Team);
	if (!Start)
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: SpawnBot - team %u has no PlayerStart"), Team);
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// SpawnActorDeferred so SetTeam runs BEFORE FinishSpawning: the NPC's
	// bAutoPossessAI possesses the bot DURING the spawn, and the StateTree
	// starts in OnPossess. If the team isn't set yet, the AI controller sees
	// team 255 and the sense task can't tell friend from foe. Deferring lets
	// us set team + spawn protection before the controller takes over.
	AShooterNPC* Bot = GetWorld()->SpawnActorDeferred<AShooterNPC>(
		BotClass, Start->GetActorTransform(), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

	if (Bot)
	{
		Bot->SetTeam(Team);

		// Requirement: spawn protection applies to bots too (anti spawn-camp),
		// so a freshly spawned bot is briefly invincible. See TDM_需求文档.md.
		Bot->GrantSpawnProtection(SpawnProtectionTime);

		Bot->FinishSpawning(Start->GetActorTransform());

		// Track it. Bots revive in place (pawn reuse); a safety timer keeps the
		// roster full if a bot is ever destroyed instead of dying normally.
		TrackedBots.Add(Bot);

		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: SpawnBot - team %u bot spawned (%s) at %s"),
			Team, *Bot->GetName(), *Start->GetName());

		// 1s later, verify the bot actually got an AI controller + state tree
		// (if not, it will just stand there — the "AI doesn't move" symptom).
		FTimerHandle DiagTimer;
		GetWorldTimerManager().SetTimer(DiagTimer, [Bot = TWeakObjectPtr<AShooterNPC>(Bot), Team]()
		{
			if (Bot.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: SpawnBot diag - %s team %u controller=%s loc=%s"),
					*Bot->GetName(), Team,
					Bot->GetController() ? *Bot->GetController()->GetName() : TEXT("NULL"),
					*Bot->GetActorLocation().ToString());
			}
		}, 1.0f, false);
	}
}

void AShooterTDMGameMode::EnsureRosterSize()
{
	// Only tops up during Playing — during the prepare phase NO bots may spawn,
	// and after the match ends nothing is respawned.
	if (bMatchEnded || MatchPhase != ETDMMatchPhase::Playing || !IsValid(BotClass))
	{
		return;
	}

	// Safety net: if a team falls below TeamSize (e.g. a bot was destroyed
	// instead of dying normally), spawn replacements. Bots that died normally
	// revive in place, so by the time this runs they are already alive again.
	for (uint8 Team = 0; Team < static_cast<uint8>(TeamSpawnTags.Num()); ++Team)
	{
		const int32 Needed = FMath::Max(0, TeamSize - CountHumanPlayers(Team) - CountAliveBots(Team));
		for (int32 i = 0; i < Needed; ++i)
		{
			SpawnBot(Team);
		}
	}
}

int32 AShooterTDMGameMode::CountHumanPlayers(uint8 Team) const
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

int32 AShooterTDMGameMode::CountAliveBots(uint8 Team) const
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

bool AShooterTDMGameMode::RemoveRandomBot(uint8 Team)
{
	// Collect the live bots of the given team.
	TArray<TWeakObjectPtr<AShooterNPC>> Candidates;
	for (const TWeakObjectPtr<AShooterNPC>& Bot : TrackedBots)
	{
		if (Bot.IsValid() && !Bot->IsDead() && Bot->GetTeamByte() == Team)
		{
			Candidates.Add(Bot);
		}
	}

	if (Candidates.Num() == 0)
	{
		return false;
	}

	// Evict a random one so the entering player takes its slot.
	const int32 VictimIndex = FMath::RandRange(0, Candidates.Num() - 1);
	if (AShooterNPC* Victim = Candidates[VictimIndex].Get())
	{
		Victim->Destroy();
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: RemoveRandomBot - team %u evicted %s"), Team, *Victim->GetName());
		return true;
	}
	return false;
}

//~ Projectile object pool ----------------------------------------------------

AShooterProjectile* AShooterTDMGameMode::AcquireProjectile(
	TSubclassOf<AShooterProjectile> Class,
	const FTransform& Transform,
	AActor* NewOwner,
	APawn* NewInstigator,
	const FName& NoiseTag)
{
	if (!Class)
	{
		return nullptr;
	}

	// Reuse an idle pooled projectile of the same class.
	for (TWeakObjectPtr<AShooterProjectile>& Weak : ProjectilePool)
	{
		if (Weak.IsValid() && !Weak->IsActive() && Weak->IsA(Class))
		{
			return Weak->ActivateFromPool(Transform, NewOwner, NewInstigator, NoiseTag);
		}
	}

	// No idle one — spawn a new projectile that will return to the pool instead
	// of being destroyed when it hits/expires.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
	SpawnParams.Owner = NewOwner;
	SpawnParams.Instigator = NewInstigator;

	AShooterProjectile* Projectile = GetWorld()->SpawnActor<AShooterProjectile>(Class, Transform, SpawnParams);
	if (Projectile)
	{
		Projectile->SetNoiseTag(NoiseTag);
		ProjectilePool.Add(Projectile);
	}
	return Projectile;
}

void AShooterTDMGameMode::ReturnProjectile(AShooterProjectile* Projectile)
{
	if (!Projectile)
	{
		return;
	}

	// Count live pooled entries; if the pool is over the cap, actually destroy.
	int32 LiveCount = 0;
	for (const TWeakObjectPtr<AShooterProjectile>& Weak : ProjectilePool)
	{
		if (Weak.IsValid())
		{
			++LiveCount;
		}
	}

	if (LiveCount > MaxPoolSize)
	{
		ProjectilePool.Remove(Projectile);
		Projectile->Destroy();
		return;
	}

	// Otherwise it stays cached (DeactivateToPool already stopped/hid it).
}
