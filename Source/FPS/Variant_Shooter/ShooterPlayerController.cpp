// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "ShooterBulletCounterUI.h"
#include "Variant_Shooter/UI/ShooterPrepareUI.h"
#include "ShooterTDMGameMode.h"
#include "ShooterTDMLoadoutUI.h"
#include "ShooterUI.h"
#include "Variant_Shooter/UI/ShooterTDMEndScreen.h"
#include "Widgets/SWidget.h"
#include "FPS.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AShooterPlayerController::AShooterPlayerController()
{
	// Default the respawn class so a destroyed pawn can ALWAYS be respawned
	// even when the Blueprint left CharacterClass unset.
	static ConstructorHelpers::FClassFinder<AShooterCharacter> PlayerBP(
		TEXT("/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	if (PlayerBP.Succeeded())
	{
		CharacterClass = PlayerBP.Class;
	}

	// Default the prepare-phase countdown widget to PrePareUI (override in the
	// Blueprint if a different widget is wanted).
	static ConstructorHelpers::FClassFinder<UUserWidget> PrepareBP(
		TEXT("/Game/Variant_Shooter/UI/PrePareUI.PrePareUI_C"));
	if (PrepareBP.Succeeded())
	{
		PrepareUIClass = PrepareBP.Class;
	}
}

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();
}

void AShooterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// add the input mapping contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}

		if (ShouldUseTouchControls())
		{
			// spawn the mobile controls widget
			MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

			if (MobileControlsWidget)
			{
				// add the controls to the player screen
				MobileControlsWidget->AddToPlayerScreen(0);

			} else {

				UE_LOG(LogFPS, Error, TEXT("Could not spawn mobile controls widget."));

			}
		}

		// create the bullet counter widget and add it to the screen
		BulletCounterUI = CreateWidget<UShooterBulletCounterUI>(this, BulletCounterUIClass);

		if (BulletCounterUI)
		{
			BulletCounterUI->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogFPS, Error, TEXT("Could not spawn bullet counter widget."));

		}

		// create the prepare-phase countdown widget (hidden until the 30s
		// countdown starts; shown/hidden by Client_OnPrepare* events)
		if (PrepareUIClass)
		{
			PrepareUI = CreateWidget<UUserWidget>(this, PrepareUIClass);
			if (PrepareUI)
			{
				PrepareUI->AddToPlayerScreen(50);
				PrepareUI->SetVisibility(ESlateVisibility::Collapsed);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: could not create prepare UI widget"));
			}
		}
	}
}

void AShooterPlayerController::SetPawn(APawn* InPawn)
{
	Super::SetPawn(InPawn);

	UE_LOG(LogTemp, Warning, TEXT("AMMO_UI: %s SetPawn pawn=%s local=%d"),
		*GetName(), *GetNameSafe(InPawn), IsLocalController() ? 1 : 0);

	// Subscribe to the pawn's delegates HERE: SetPawn runs on the server (via
	// OnPossess) AND on the owning client (via ClientRestart). OnPossess alone
	// never runs client-side, which is why the HUD never received events in a
	// dedicated-server setup.
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		// add the player tag
		ShooterCharacter->Tags.Add(PlayerPawnTag);

		// (re)subscribe idempotently — SetPawn can be called multiple times
		// (possession, re-possession), avoid duplicate handler invocation.
		ShooterCharacter->OnBulletCountUpdated.RemoveDynamic(this, &AShooterPlayerController::OnBulletCountUpdated);
		ShooterCharacter->OnBulletCountUpdated.AddDynamic(this, &AShooterPlayerController::OnBulletCountUpdated);
		ShooterCharacter->OnDamaged.RemoveDynamic(this, &AShooterPlayerController::OnPawnDamaged);
		ShooterCharacter->OnDamaged.AddDynamic(this, &AShooterPlayerController::OnPawnDamaged);
		ShooterCharacter->OnReloadStateChanged.RemoveDynamic(this, &AShooterPlayerController::OnReloadStateChanged);
		ShooterCharacter->OnReloadStateChanged.AddDynamic(this, &AShooterPlayerController::OnReloadStateChanged);
		InPawn->OnDestroyed.RemoveDynamic(this, &AShooterPlayerController::OnPawnDestroyed);
		InPawn->OnDestroyed.AddDynamic(this, &AShooterPlayerController::OnPawnDestroyed);

		// force update the life bar + ammo counter. The HUD lives on the
		// locally controlled PC; replication OnReps can fire BEFORE this PC
		// subscribed, so pull the current state explicitly now.
		if (IsLocalController())
		{
			ShooterCharacter->OnDamaged.Broadcast(1.0f);
			if (AShooterWeapon* Weapon = ShooterCharacter->GetCurrentWeapon())
			{
				UE_LOG(LogTemp, Warning, TEXT("AMMO_UI: %s SetPawn force-sync ammo=%d mag=%d"),
					*GetName(), Weapon->GetBulletCount(), Weapon->GetMagazineSize());
				ShooterCharacter->OnBulletCountUpdated.Broadcast(Weapon->GetMagazineSize(), Weapon->GetBulletCount());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("AMMO_UI: %s SetPawn force-sync: no weapon yet (GetCurrentWeapon=null)"),
					*GetName());
			}
		}
	}
}

void AShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	UE_LOG(LogTemp, Warning, TEXT("AMMO_UI: %s OnPossess pawn=%s local=%d (subscriptions happen in SetPawn)"),
		*GetName(), *GetNameSafe(InPawn), IsLocalController() ? 1 : 0);

	// is this a shooter character? (server-side: assign team + TDM flow)
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		// set the team
		ShooterCharacter->SetTeam(TeamByte);

		// TDM flow (DS-safe, server-authoritative):
		//  - Player NOT ready yet => show the pre-match menu (each player must
		//    click "start" to enter; input freeze is applied by the server).
		//  - Ready + playing      => open the respawn loadout (weapon server-side).
		//  - Match ended          => settlement screen already shown, show nothing.
		if (AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
		{
			if (TDM->GetMatchPhase() == ETDMMatchPhase::Ended)
			{
				// Nothing to do here; EndMatch pushed the settlement screen.
			}
			else if (IsMatchReady() && TDM->IsMatchPlaying())
			{
				OpenLoadoutUI(ShooterCharacter);
			}
			else
			{
				Client_ShowMainMenu(TDM->MainMenuClass);
			}
		}
	}
}

void AShooterPlayerController::Client_ShowMainMenu_Implementation(TSubclassOf<UUserWidget> InMenuClass)
{
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: Client_ShowMainMenu - class=%s"), *GetNameSafe(InMenuClass));

	if (InMenuClass && !IsValid(MainMenuWidget))
	{
		MainMenuWidget = CreateWidget<UUserWidget>(this, InMenuClass);
		if (MainMenuWidget)
		{
			MainMenuWidget->AddToViewport(100);
			// Hand focus to the widget so the FIRST click lands on the button
			// (otherwise the first click is consumed by focus acquisition).
			MainMenuWidget->SetUserFocus(this);
			UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: Client_ShowMainMenu - widget created & shown"));
		}
	}

	// Input freeze is server-authoritative (PostLogin replicated flags); here we
	// only switch to UI mode + show the cursor for the menu overlay.
	bShowMouseCursor = true;
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(IsValid(MainMenuWidget) ? MainMenuWidget->TakeWidget() : TSharedPtr<SWidget>());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

void AShooterPlayerController::Client_OnMatchStarted_Implementation(TSubclassOf<UUserWidget> InHudClass)
{
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: Client_OnMatchStarted"));

	if (IsValid(MainMenuWidget))
	{
		MainMenuWidget->RemoveFromParent();
		MainMenuWidget = nullptr;
	}

	// Build the shooter HUD on the client (invisible if created on a server).
	if (InHudClass && !IsValid(ShooterUI))
	{
		ShooterUI = CreateWidget<UShooterUI>(this, InHudClass);
		if (ShooterUI)
		{
			ShooterUI->AddToViewport(0);
		}
	}

	// Restore game input AND re-attach the camera to the pawn (the menu's UI
	// mode can leave the view detached).
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	if (APawn* ViewPawn = GetPawn())
	{
		SetViewTarget(ViewPawn);
	}
}

void AShooterPlayerController::Client_ShowEndScreen_Implementation(uint8 WinningTeam, int32 RedScore, int32 BlueScore, TSubclassOf<UUserWidget> InEndScreenClass)
{
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: Client_ShowEndScreen - class=%s, win=%u, %d-%d"),
		*GetNameSafe(InEndScreenClass), WinningTeam, RedScore, BlueScore);

	UUserWidget* Widget = nullptr;
	if (InEndScreenClass)
	{
		Widget = CreateWidget<UUserWidget>(this, InEndScreenClass);
		if (Widget)
		{
			Widget->AddToViewport(100);
			Widget->SetUserFocus(this);
			if (UShooterTDMEndScreen* End = Cast<UShooterTDMEndScreen>(Widget))
			{
				End->Populate(WinningTeam, RedScore, BlueScore);
			}
		}
	}

	bShowMouseCursor = true;
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Widget ? Widget->TakeWidget() : TSharedPtr<SWidget>());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

void AShooterPlayerController::OpenLoadoutUI(AShooterCharacter* ShooterCharacter)
{
	if (!ShooterCharacter)
	{
		return;
	}

	// No loadout panel configured — hand out the default weapon directly so the
	// player can always fight. (OpenLoadout picks loadout[0] or the fallback gun.)
	if (!LoadoutUIClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s OpenLoadoutUI - LoadoutUIClass null, equipping default weapon directly"), *GetName());
		ShooterCharacter->OpenLoadout();
		ShooterCharacter->CloseLoadout();
		return;
	}

	// drop any stale widget from a previous life
	if (IsValid(LoadoutUI))
	{
		LoadoutUI->RemoveFromParent();
		LoadoutUI = nullptr;
	}

	LoadoutUI = CreateWidget<UShooterTDMLoadoutUI>(this, LoadoutUIClass);
	if (LoadoutUI)
	{
		LoadoutUI->SetOwningCharacter(ShooterCharacter);
		if (AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
		{
			LoadoutUI->BP_Populate(TDM->GetLoadoutWeapons());
		}
		LoadoutUI->BP_Show();
		ShooterCharacter->OpenLoadout();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s OpenLoadoutUI - CreateWidget failed"), *GetName());
	}
}

void AShooterPlayerController::OnPawnDestroyed(AActor* DestroyedActor)
{
	// Server-authoritative respawn: only the server spawns the replacement
	// pawn. The client's copy of this delegate fires when the replicated pawn
	// is destroyed locally; it must NOT respawn (the server will replicate the
	// new pawn).
	if (!HasAuthority())
	{
		return;
	}

	// reset the bullet counter HUD
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_UpdateBulletCounter(0, 0);
	}

	// Find a spawn point for this player's team. Use the TDM game mode's
	// numbered-start lookup (PlayerStart 0-4 = RED, 5-8 = BLUE) so a destroyed
	// pawn can ALWAYS be respawned even when the level has no RED/BLUE tags.
	AActor* SpawnPoint = nullptr;
	if (AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
	{
		SpawnPoint = TDM->FindTeamPlayerStart(TeamByte);
	}
	if (!SpawnPoint && TeamByte < TeamTags.Num())
	{
		// fallback: tagged player starts (legacy layout)
		TArray<AActor*> ActorList;
		UGameplayStatics::GetAllActorsOfClassWithTag(GetWorld(), APlayerStart::StaticClass(), TeamTags[TeamByte], ActorList);
		if (ActorList.Num() > 0)
		{
			SpawnPoint = ActorList[FMath::RandRange(0, ActorList.Num() - 1)];
		}
	}

	if (SpawnPoint)
	{
		// spawn a character at the player start
		const FTransform SpawnTransform = SpawnPoint->GetActorTransform();

		// Fallback: if CharacterClass is unset, use the game mode's default pawn.
		TSubclassOf<AShooterCharacter> PawnClass = CharacterClass;
		if (!PawnClass)
		{
			if (const AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
			{
				PawnClass = TDM->DefaultPawnClass;
			}
		}

		if (PawnClass)
		{
			AShooterCharacter* RespawnedCharacter = GetWorld()->SpawnActor<AShooterCharacter>(PawnClass, SpawnTransform);
			if (RespawnedCharacter)
			{
				// REQUIREMENT: assign the team to the NEW body so team coloring
				// and friendly-fire checks are correct for this pawn.
				RespawnedCharacter->SetTeam(TeamByte);

				UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s respawned %s at %s (team %u)"),
					*GetName(), *RespawnedCharacter->GetName(), *SpawnPoint->GetName(), TeamByte);
				// possess the character
				Possess(RespawnedCharacter);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s respawn FAILED (SpawnActor returned null, class=%s)"),
					*GetName(), *GetNameSafe(PawnClass));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s respawn FAILED (no pawn class)"), *GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s respawn FAILED (no spawn point for team %u)"), *GetName(), TeamByte);
	}
}

void AShooterPlayerController::OnBulletCountUpdated(int32 MagazineSize, int32 Bullets)
{
	UE_LOG(LogTemp, Warning, TEXT("AMMO_UI: %s PC received ammo event mag=%d cur=%d UI=%s"),
		*GetName(), MagazineSize, Bullets, IsValid(BulletCounterUI) ? TEXT("VALID") : TEXT("NULL"));

	// update the UI
	if (BulletCounterUI)
	{
		BulletCounterUI->BP_UpdateBulletCounter(MagazineSize, Bullets);
	}
}

void AShooterPlayerController::OnPawnDamaged(float LifePercent)
{
	UE_LOG(LogTemp, Warning, TEXT("HP_UI: %s PC received health event ratio=%.2f UI=%s"),
		*GetName(), LifePercent, IsValid(BulletCounterUI) ? TEXT("VALID") : TEXT("NULL"));

	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_Damaged(LifePercent);
	}
}

void AShooterPlayerController::OnReloadStateChanged(bool bReloading)
{
	// The weapon's replicated reload state changed — forward to the HUD widget
	// (the Blueprint shows/hides a "reloading" indicator).
	if (IsValid(BulletCounterUI))
	{
		BulletCounterUI->BP_OnReloadStateChanged(bReloading);
	}
}

void AShooterPlayerController::Client_OnPrepareCountdown_Implementation(int32 SecondsRemaining)
{
	// Show the prepare widget (in case it starts hidden) and forward the
	// seconds to the widget's Blueprint event. The PC's own Blueprint event
	// is also fired for convenience (either place can drive the UI).
	if (UShooterPrepareUI* UI = Cast<UShooterPrepareUI>(PrepareUI))
	{
		UI->SetVisibility(ESlateVisibility::Visible);
		UI->BP_OnPrepareCountdown(SecondsRemaining);
	}
	BP_OnPrepareCountdown(SecondsRemaining);
}

void AShooterPlayerController::Client_OnPrepareEnded_Implementation()
{
	if (UShooterPrepareUI* UI = Cast<UShooterPrepareUI>(PrepareUI))
	{
		UI->SetVisibility(ESlateVisibility::Collapsed);
		UI->BP_OnPrepareEnded();
	}
	BP_OnPrepareEnded();
}

bool AShooterPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void AShooterPlayerController::SetTeam(uint8 Team)
{
	TeamByte = Team;

	// if we already have a pawn, set its team
	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetPawn()))
	{
		ShooterCharacter->SetTeam(Team);
	}
}

void AShooterPlayerController::ServerChooseTeam_Implementation(uint8 Team)
{
	if (!HasAuthority())
	{
		return;
	}
	if (Team > 1)
	{
		return;
	}

	// REQUIREMENT: the player picks a team in the start menu; the chosen team
	// is applied BEFORE any (re)spawn so the player always revives on their
	// own side.
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s chose team %u"), *GetName(), Team);
	SetTeam(Team);
}

void AShooterPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterPlayerController, TeamByte);
}

void AShooterPlayerController::OnRep_TeamByte()
{
	// The client now knows its own team — the HUD scoreboard can orient itself
	// (own team LEFT, enemy RIGHT). UI refreshes itself on demand via GetTeam().
}

void AShooterPlayerController::SetTeamTags(const TArray<FName>& InTags)
{
	TeamTags = InTags;
}

void AShooterPlayerController::HideLoadoutWidget()
{
	if (IsValid(LoadoutUI))
	{
		LoadoutUI->BP_Hide();
	}
}

void AShooterPlayerController::Client_UpdateScore_Implementation(uint8 Team, int32 Score)
{
	// DS-correct score push: the HUD lives on the client, so the server's
	// ReportKill broadcasts here instead of touching a server-side widget.
	if (IsValid(ShooterUI))
	{
		ShooterUI->BP_UpdateScore(Team, Score);
	}
}

void AShooterPlayerController::ServerStartMatch_Implementation()
{
	// Runs on the server (the client's connection is owned by this PC).
	if (!HasAuthority())
	{
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s ServerStartMatch RPC received"), *GetName());
	if (AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// Only THIS player enters — others stay on the menu until they click too.
		TDM->StartPlayerMatch(this);
	}
}

void AShooterPlayerController::ServerReturnToMainMenu_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s ServerReturnToMainMenu RPC received"), *GetName());
	if (AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
	{
		TDM->ReturnToMainMenu();
	}
}
