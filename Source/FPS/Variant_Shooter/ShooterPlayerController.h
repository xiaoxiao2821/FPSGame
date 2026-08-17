// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShooterPlayerController.generated.h"

class UInputMappingContext;
class AShooterCharacter;
class UShooterBulletCounterUI;
class UShooterTDMLoadoutUI;
class UShooterUI;
class UUserWidget;

/**
 *  Simple PlayerController for a first person shooter game
 *  Manages input mappings
 *  Respawns the player pawn when it's destroyed
 */
UCLASS(abstract, config="Game")
class FPS_API AShooterPlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input mapping contexts for this player */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Character class to respawn when the possessed pawn is destroyed.
	 *  Defaults to BP_ShooterCharacter (constructor) so a destroyed pawn can
	 *  ALWAYS be respawned even if the Blueprint left this unset. */
	UPROPERTY(EditAnywhere, Category="Shooter|Respawn")
	TSubclassOf<AShooterCharacter> CharacterClass;

	/** Type of bullet counter UI widget to spawn */
	UPROPERTY(EditAnywhere, Category="Shooter|UI")
	TSubclassOf<UShooterBulletCounterUI> BulletCounterUIClass;

	/** Tag to grant the possessed pawn to flag it as the player */
	UPROPERTY(EditAnywhere, Category="Shooter|Player")
	FName PlayerPawnTag = FName("Player");

	/** Pointer to the bullet counter UI widget */
	UPROPERTY()
	TObjectPtr<UShooterBulletCounterUI> BulletCounterUI;

	/** Type of respawn loadout UI widget to spawn (TDM only) */
	UPROPERTY(EditAnywhere, Category="Shooter|UI")
	TSubclassOf<UShooterTDMLoadoutUI> LoadoutUIClass;

	/** Pointer to the respawn loadout UI widget */
	UPROPERTY()
	TObjectPtr<UShooterTDMLoadoutUI> LoadoutUI;

	/** Pre-match main menu widget (created client-side, DS-safe). */
	UPROPERTY()
	TObjectPtr<UUserWidget> MainMenuWidget;

	/** Shooter HUD (bullet counter + scoreboard, created client-side, DS-safe). */
	UPROPERTY()
	TObjectPtr<UShooterUI> ShooterUI;

	/** Team ID for this player (replicated so the client HUD knows its own
	 *  side — scoreboard shows own team on the LEFT, enemy on the RIGHT). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_TeamByte, Category="Shooter|Team")
	uint8 TeamByte = 0;

	/** Called on clients when the replicated team arrives. */
	UFUNCTION()
	void OnRep_TeamByte();

	/** Replication (TeamByte for client-side scoreboard orientation). */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Tags that identify each team for PlayerStart selection upon respawning */
	UPROPERTY(EditAnywhere, Category="Shooter|Team")
	TArray<FName> TeamTags;

	/** True once THIS player has clicked "start" and may enter the match.
	 *  Server-authoritative (never replicated): each player enters independently. */
	bool bMatchReady = false;

protected:

	/** Gameplay Initialization */
	virtual void BeginPlay() override;

	/** Initialize input bindings */
	virtual void SetupInputComponent() override;

	/** Widget class for the prepare-phase countdown UI (defaults to PrePareUI). */
	UPROPERTY(EditAnywhere, Category = "Shooter|UI")
	TSubclassOf<UUserWidget> PrepareUIClass;

	/** Currently displayed prepare countdown widget (hidden until it starts). */
	UPROPERTY()
	TObjectPtr<UUserWidget> PrepareUI;

	/** Pawn initialization (server side only — runs via Possess). */
	virtual void OnPossess(APawn* InPawn) override;

	/** Pawn assignment — runs on BOTH the server (via OnPossess) AND the owning
	 *  client (via ClientRestart), so delegate subscriptions and the initial
	 *  HUD sync must live HERE, not in OnPossess. */
	virtual void SetPawn(APawn* InPawn) override;

	/** Called if the possessed pawn is destroyed */
	UFUNCTION()
	void OnPawnDestroyed(AActor* DestroyedActor);

	/** Called when the bullet count on the possessed pawn is updated */
	UFUNCTION()
	void OnBulletCountUpdated(int32 MagazineSize, int32 Bullets);

	/** Called when the possessed pawn is damaged */
	UFUNCTION()
	void OnPawnDamaged(float LifePercent);

	/** Called when the possessed pawn's weapon starts/finishes reloading */
	UFUNCTION()
	void OnReloadStateChanged(bool bReloading);

	/** Blueprint hook: update the prepare countdown display (e.g. "30s"). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|HUD")
	void BP_OnPrepareCountdown(int32 SecondsRemaining);

	/** Blueprint hook: hide the prepare countdown (match has started). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter|HUD")
	void BP_OnPrepareEnded();

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

public:

	/** Constructor (defaults CharacterClass to BP_ShooterCharacter). */
	AShooterPlayerController();

	/** Server -> owning client: one tick of the prepare-phase countdown
	 *  (public: called by the GameMode while broadcasting the countdown). */
	UFUNCTION(Client, Reliable)
	void Client_OnPrepareCountdown(int32 SecondsRemaining);

	/** Server -> owning client: prepare phase ended (countdown UI hides). */
	UFUNCTION(Client, Reliable)
	void Client_OnPrepareEnded();

	/** Assigns a team ID to this player */
	void SetTeam(uint8 Team);

	/** Client -> server: the player picked a team in the start menu (0=RED,
	 *  1=BLUE). Call from the menu's team-selection buttons. */
	UFUNCTION(Server, Reliable)
	void ServerChooseTeam(uint8 Team);

	/** Returns the team ID assigned to this player */
	uint8 GetTeam() const { return TeamByte; }

	/** Overrides the per-team PlayerStart tags used for respawning */
	void SetTeamTags(const TArray<FName>& InTags);

	/** Marks this player as ready to enter the match (server-side). */
	void SetMatchReady(bool bReady) { bMatchReady = bReady; }

	/** True once this player clicked "start" (server-side). */
	bool IsMatchReady() const { return bMatchReady; }

	/** Hides and clears the respawn loadout widget (called by the possessed character on close) */
	void HideLoadoutWidget();

	/** Opens the respawn loadout panel for the given character (match start + respawn). */
	void OpenLoadoutUI(AShooterCharacter* ShooterCharacter);

	/** Server->Client: show the pre-match main menu on this client (DS-safe). */
	UFUNCTION(Client, Reliable)
	void Client_ShowMainMenu(TSubclassOf<UUserWidget> InMenuClass);

	/** Server->Client: hide the menu, build the HUD and re-attach the view to the pawn. */
	UFUNCTION(Client, Reliable)
	void Client_OnMatchStarted(TSubclassOf<UUserWidget> InHudClass);

	/** Server->Client: show the end-of-match settlement screen (DS-safe). */
	UFUNCTION(Client, Reliable)
	void Client_ShowEndScreen(uint8 WinningTeam, int32 RedScore, int32 BlueScore, TSubclassOf<UUserWidget> InEndScreenClass);

	/** Server->Client: push the latest score of a team to this client's HUD. */
	UFUNCTION(Client, Reliable)
	void Client_UpdateScore(uint8 Team, int32 Score);

	/** Client->Server: start the match (DS-safe entry; call from menu button).
	 *  Each player calls this independently — only the caller enters the match. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "TDM")
	void ServerStartMatch();

	/** Client->Server: restart the level back to the main menu (DS-safe entry). */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "TDM")
	void ServerReturnToMainMenu();
};
