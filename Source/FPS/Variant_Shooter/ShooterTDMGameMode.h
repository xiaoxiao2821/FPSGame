// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterGameMode.h"
#include "ShooterTDMGameMode.generated.h"

class AShooterWeapon;
class AShooterProjectile;
class AShooterPlayerController;
class AShooterNPC;
class UUserWidget;

UENUM(BlueprintType)
enum class ETDMMatchPhase : uint8
{
	MainMenu	UMETA(DisplayName = "Main Menu"),
	Prepare		UMETA(DisplayName = "Prepare (countdown)"),
	Playing		UMETA(DisplayName = "Playing"),
	Ended		UMETA(DisplayName = "Ended")
};

/**
 *  Team Deathmatch game mode (参考和平精英"经典团竞").
 *
 *  - 双方出生点分别在地图两边 (TeamSpawnTags[0] / [1])。
 *  - 击杀敌方玩家累计到 KillTarget 即获胜 (默认 30)。
 *  - 4v4 对抗，人类不足时由 AShooterTDMSpawner 用 AI 补齐。
 *  - 复活时有 SpawnProtectionTime 秒无敌，防止出生点被压制。
 *  - 复活时可调整武器 (配枪浮层)，移动或射击后浮层关闭。
 *  - 流程：主菜单(开始) -> 对战 -> 结算屏(返回菜单)。
 *
 *  计分语义：点数加在 *击杀者* 队伍上（而非死者队伍）；同队误杀 / 自杀不计分。
 */
UCLASS()
class FPS_API AShooterTDMGameMode : public AShooterGameMode
{
	GENERATED_BODY()

protected:

	/** True once a team has reached KillTarget and the match is over. */
	bool bMatchEnded = false;

	/** Current match phase (drives menu / play / end-screen flow). */
	ETDMMatchPhase MatchPhase = ETDMMatchPhase::MainMenu;

	/** Round-robin slot used to assign teams to joining players. */
	int32 NextTeamSlot = 0;

public:

	/** Constructor (sets up the default bot class). */
	AShooterTDMGameMode();

	/** Number of kills needed to win the match */
	UPROPERTY(EditAnywhere, Category = "TDM", meta = (ClampMin = 1, ClampMax = 999))
	int32 KillTarget = 30;

	/** Desired players (human + AI) per team. Human shortfall is filled by AI. */
	UPROPERTY(EditAnywhere, Category = "TDM", meta = (ClampMin = 1, ClampMax = 16))
	int32 TeamSize = 4;

	/** Seconds of spawn protection granted on (re)spawn */
	UPROPERTY(EditAnywhere, Category = "TDM", meta = (ClampMin = 0, ClampMax = 30))
	float SpawnProtectionTime = 3.0f;

	/** PlayerStart actor tags, one per team (index 0 / 1). Must match the level. */
	UPROPERTY(EditAnywhere, Category = "TDM")
	TArray<FName> TeamSpawnTags = { FName("RED"), FName("BLUE") };

	/** Weapons the player may choose from on respawn (first entry = default). */
	UPROPERTY(EditAnywhere, Category = "TDM")
	TArray<TSubclassOf<AShooterWeapon>> LoadoutWeapons;

	/** Widget class for the pre-match main menu. */
	UPROPERTY(EditAnywhere, Category = "TDM|UI")
	TSubclassOf<UUserWidget> MainMenuClass;

	/** Widget class for the end-of-match settlement screen. */
	UPROPERTY(EditAnywhere, Category = "TDM|UI")
	TSubclassOf<UUserWidget> EndScreenClass;

	/** NPC class spawned to fill a team up to TeamSize (defaults to BP_ShooterNPC). */
	UPROPERTY(EditAnywhere, Category = "TDM|AI")
	TSubclassOf<AShooterNPC> BotClass;

	/** Delay before a dead bot is replaced. */
	UPROPERTY(EditAnywhere, Category = "TDM|AI", meta = (ClampMin = 0, ClampMax = 30))
	float BotRespawnDelay = 3.0f;

	/** Length of the prepare phase (countdown after the first player enters).
	 *  During it: NO bots are spawned and ALL damage is ignored. */
	UPROPERTY(EditAnywhere, Category = "TDM", meta = (ClampMin = 1, ClampMax = 120, Units = "s"))
	float PrepareTime = 30.0f;

	//~ Team spawn locations — EXPLICIT transforms that do NOT depend on level
	// streaming. World Partition only loads PlayerStarts near the player, so
	// scanning can find just 1 of 8. Fill these (4 per team) in the GameMode
	// Blueprint to guarantee all spawn points are always available; when empty,
	// the code falls back to scanning the level's PlayerStarts.

	/** Red team spawn point transforms (round-robin 0→1→2→3). */
	UPROPERTY(EditAnywhere, Category = "TDM|Spawns")
	TArray<FTransform> RedSpawnLocations;

	/** Blue team spawn point transforms (round-robin 0→1→2→3). */
	UPROPERTY(EditAnywhere, Category = "TDM|Spawns")
	TArray<FTransform> BlueSpawnLocations;

	/** Awards a kill to the killer's team and checks for a win. */
	virtual void ReportKill(uint8 KillerTeam) override;

	/** Picks a PlayerStart on the requesting player's team. */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** Finds the NEXT PlayerStart for the given team in round-robin order
	 *  (requirement: spawn points are used SEQUENTIALLY so units don't pile up
	 *  on one spot). Uses the explicit Red/BlueSpawnLocations when configured,
	 *  otherwise scans level PlayerStarts (numbered names 1-4 RED / 5-8 BLUE). */
	AActor* FindTeamPlayerStart(uint8 Team);

	/** Per-team round-robin cursor for sequential spawn-point assignment. */
	int32 NextPlayerStartIndex[2] = { 0, 0 };

	/** Spawned once at BeginPlay from Red/BlueSpawnLocations so they are immune
	 *  to World Partition streaming. [0] = RED, [1] = BLUE. */
	TArray<TObjectPtr<AActor>> ResolvedSpawnPoints[2];

	/** TDM manages its own AI fillers; do not use the legacy enemy-NPC path. */
	virtual bool ShouldSpawnEnemyNPCs() const override { return false; }

	/** Assigns a team (round-robin) and pushes spawn tags to each joining player. */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/** Assigns a team (round-robin) as early as possible — right after the
	 *  base Login creates the PlayerController and BEFORE the base PostLogin
	 *  restarts the player (which spawns the pawn). Without this, every
	 *  player's first spawn lands on RED (the "everyone on the same side
	 *  during prepare" bug). */
	virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;

protected:

	/** Gameplay initialization: shows the main menu and freezes players. */
	virtual void BeginPlay() override;

	/** Ends the match and notifies Blueprints. */
	void EndMatch(uint8 WinningTeam);

public:

	/** Seconds of spawn protection (read by characters on spawn). */
	float GetSpawnProtectionTime() const { return SpawnProtectionTime; }

	/** Kill goal for the match. */
	int32 GetKillTarget() const { return KillTarget; }

	/** Desired roster size per team. */
	int32 GetTeamSize() const { return TeamSize; }

	/** The weapon classes offered in the respawn loadout (first = default). */
	const TArray<TSubclassOf<AShooterWeapon>>& GetLoadoutWeapons() const { return LoadoutWeapons; }

	/** True if the match has concluded. */
	bool IsMatchEnded() const { return bMatchEnded; }

	/** True if the match is currently in the playing phase. */
	bool IsMatchPlaying() const { return MatchPhase == ETDMMatchPhase::Playing; }

	/** Current match phase. */
	UFUNCTION(BlueprintPure, Category = "TDM")
	ETDMMatchPhase GetMatchPhase() const { return MatchPhase; }

	/** True while the 30s prepare countdown is running (no bots, no damage). */
	bool IsPreparePhase() const { return MatchPhase == ETDMMatchPhase::Prepare; }

	/** Seconds left in the prepare countdown (0 when not preparing). */
	int32 GetPrepareRemainingSeconds() const { return PrepareRemainingSeconds; }

	/** Begins the match: hides the menu, enables input, opens loadouts.
	 *  SERVER-ONLY: call via AShooterPlayerController::ServerStartMatch(). */
	UFUNCTION(BlueprintCallable, Category = "TDM")
	void StartMatch();

	/** Lets ONE player enter the match (they clicked "start"): unfreezes them,
	 *  drops their menu, equips their weapon. The first entrant also starts the
	 *  PREPARE phase (30s countdown, no bots, no damage); bots are filled only
	 *  after the countdown completes.
	 *  SERVER-ONLY, called by AShooterPlayerController::ServerStartMatch. */
	void StartPlayerMatch(AShooterPlayerController* PC);

	/** Starts the prepare phase (first player entered): MatchPhase -> Prepare,
	 *  starts the 30s countdown and streams it to every client's UI. */
	void StartPreparePhase();

	/** Per-second countdown tick during the prepare phase. */
	void TickPrepareCountdown();

	/** Countdown finished: MatchPhase -> Playing, fill both teams with bots,
	 *  damage immunity is lifted and clients are told the prepare UI is done. */
	void CompletePreparePhase();

	/** Sends the current remaining seconds to every client (UI countdown). */
	void BroadcastPrepareCountdown();

	/** Timer driving the prepare countdown. */
	FTimerHandle PrepareTimer;

	/** Seconds left in the prepare countdown (server-authoritative). */
	int32 PrepareRemainingSeconds = 0;

	/** Returns to the main menu (restarts the current level).
	 *  SERVER-ONLY: call via AShooterPlayerController::ServerReturnToMainMenu(). */
	UFUNCTION(BlueprintCallable, Category = "TDM")
	void ReturnToMainMenu();

	/** Blueprint hook fired when a team reaches the kill target. */
	UFUNCTION(BlueprintImplementableEvent, Category = "TDM")
	void BP_OnMatchEnded(uint8 WinningTeam);

	/** Blueprint hook fired when the match starts (after the main menu). */
	UFUNCTION(BlueprintImplementableEvent, Category = "TDM")
	void BP_OnMatchStarted();

	//~ AI filler (built-in so a level without a spawner actor still gets bots)

	/** Fills EVERY team up to TeamSize with bots (regardless of humans) — used
	 *  at match start so both sides are full AI before players enter. */
	void FillTeamRoster();

	/** After a player enters, randomly removes one live bot from that player's
	 *  team so the human takes the bot's roster slot. */
	void ReplaceBotWithPlayer(AShooterPlayerController* PC);

	/** Spawns one bot on the given team at a tagged PlayerStart. */
	void SpawnBot(uint8 Team);

	/** Safety net: tops a team back up to TeamSize if bots were destroyed
	 *  instead of dying normally (normal deaths revive the same pawn). */
	void EnsureRosterSize();

	/** Counts human player controllers on the given team. */
	int32 CountHumanPlayers(uint8 Team) const;

	/** Counts still-alive tracked bots on the given team. */
	int32 CountAliveBots(uint8 Team) const;

	/** Randomly destroys one live bot of the given team (true if one was removed). */
	bool RemoveRandomBot(uint8 Team);

	/** Bots we have spawned, so we can count the living and top up on death. */
	UPROPERTY()
	TArray<TWeakObjectPtr<AShooterNPC>> TrackedBots;

	/** Timer for the periodic roster safety net (EnsureRosterSize). */
	FTimerHandle RosterSafetyTimer;

	//~ Projectile object pool (avoids per-shot spawn/destroy churn -> hitches)

	/** Acquires a projectile from the pool (or spawns one if none idle). */
	AShooterProjectile* AcquireProjectile(TSubclassOf<AShooterProjectile> Class, const FTransform& Transform, AActor* NewOwner, APawn* NewInstigator, const FName& NoiseTag);

	/** Returns a projectile to the pool for reuse (destroyed only if over the cap). */
	void ReturnProjectile(AShooterProjectile* Projectile);

	/** Idle pooled projectiles, reused instead of destroyed. */
	UPROPERTY()
	TArray<TWeakObjectPtr<AShooterProjectile>> ProjectilePool;

	/** Hard cap for pooled projectiles before falling back to destroy. */
	static constexpr int32 MaxPoolSize = 64;
};
