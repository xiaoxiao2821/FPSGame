// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "ShooterNPC.h"
#include "ShooterTDMGameMode.h"
#include "ShooterPlayerController.h"
#include "EnhancedInputComponent.h"
#include "Components/InputComponent.h"
#include "Components/PawnNoiseEmitterComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "ShooterGameMode.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"
#include "Animation/AnimInstance.h"

AShooterCharacter::AShooterCharacter()
{
	// create the noise emitter component
	PawnNoiseEmitter = CreateDefaultSubobject<UPawnNoiseEmitterComponent>(TEXT("Pawn Noise Emitter"));

	// configure movement
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 600.0f, 0.0f);

	// Fallback weapon so a TDM player always has a gun even when the loadout
	// list is empty or no loadout panel is configured.
	static ConstructorHelpers::FClassFinder<AShooterWeapon> DefaultWeaponBP(
		TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C"));
	if (DefaultWeaponBP.Succeeded())
	{
		DefaultWeaponClass = DefaultWeaponBP.Class;
	}
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	// reset HP to max
	CurrentHP = MaxHP;

	// Remember the mesh pose/profile at spawn so revive can undo ragdoll.
	InitialMeshTransform = GetMesh()->GetRelativeTransform();
	InitialMeshCollisionProfile = GetMesh()->GetCollisionProfileName();
	InitialFPCameraTransform = GetFirstPersonCameraComponent()->GetRelativeTransform();
	InitialFPMeshTransform = GetFirstPersonMesh()->GetRelativeTransform();

	// Force the capsule to block the channels used by aim traces and projectiles
	// (on every machine). A Blueprint profile override could otherwise silently
	// break player-vs-player damage: the client trace wouldn't hit the player
	// (aim) or the server sweep wouldn't detect the capsule (hit).
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);

	// TDM: grant spawn protection so a freshly (re)spawned player can't be spawn-camped.
	if (const AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GrantSpawnProtection(TDM->GetSpawnProtectionTime());
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s BeginPlay - spawn protection %.1fs, loadout weapons=%d"),
			*GetName(), TDM->GetSpawnProtectionTime(), TDM->GetLoadoutWeapons().Num());
	}

	// update the HUD (owning client only)
	BroadcastHealthRatio(1.0f);
}

void AShooterCharacter::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// base class handles move, aim and jump inputs
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Firing
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartFiring);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopFiring);

		// Switch weapon
		EnhancedInputComponent->BindAction(SwitchWeaponAction, ETriggerEvent::Triggered, this, &AShooterCharacter::DoSwitchWeapon);
	}

}

float AShooterCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Server-authoritative: damage is only applied on the server. A client-side
	// ApplyDamage (e.g. a locally-simulated projectile) is ignored, so all
	// clients see the same HP/death state.
	if (!HasAuthority())
	{
		return 0.0f;
	}

	// Requirement: during the PREPARE phase (30s countdown) ALL damage is
	// ignored — no one can hurt anyone until the match actually starts.
	if (const AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
	{
		if (TDM->IsPreparePhase())
		{
			UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s TakeDamage ignored (prepare phase)"), *GetName());
			return 0.0f;
		}
	}

	// ignore if already dead
	if (CurrentHP <= 0.0f)
	{
		return 0.0f;
	}

	// ignore damage while spawn-protected (TDM invincibility window)
	if (bIsSpawnProtected)
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s TakeDamage ignored (spawn protected, %.2f dmg)"), *GetName(), Damage);
		return 0.0f;
	}

	// Friendly fire immunity (requirement): a same-team attacker deals no damage.
	if (EventInstigator)
	{
		uint8 AttackerTeam = 255;
		if (const AShooterCharacter* AttackerChar = Cast<AShooterCharacter>(EventInstigator->GetPawn()))
		{
			AttackerTeam = AttackerChar->GetTeamByte();
		}
		else if (const AShooterNPC* AttackerNPC = Cast<AShooterNPC>(EventInstigator->GetPawn()))
		{
			AttackerTeam = AttackerNPC->GetTeamByte();
		}
		if (AttackerTeam != 255 && AttackerTeam == TeamByte)
		{
			return 0.0f;
		}
	}

	// Reduce HP
	CurrentHP -= Damage;
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s TakeDamage %.2f -> HP %.1f"), *GetName(), Damage, CurrentHP);

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		Die(EventInstigator);
	}

	// update the HUD (only meaningful on the owning client — on the server or
	// remote proxies the owning client already refreshed via OnRep_CurrentHP)
	BroadcastHealthRatio(FMath::Max(0.0f, CurrentHP / MaxHP));

	return Damage;
}

void AShooterCharacter::DoAim(float Yaw, float Pitch)
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoAim(Yaw, Pitch);
	}
}

void AShooterCharacter::DoMove(float Right, float Forward)
{
	// moving closes the respawn loadout panel (and commits the chosen weapon)
	if (bLoadoutOpen)
	{
		CloseLoadout();
	}

	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoMove(Right, Forward);
	}
}

void AShooterCharacter::DoJumpStart()
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoJumpStart();
	}
}

void AShooterCharacter::DoJumpEnd()
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoJumpEnd();
	}
}

void AShooterCharacter::DoStartFiring()
{
	// shooting closes the respawn loadout panel (and commits the chosen weapon)
	if (bLoadoutOpen)
	{
		CloseLoadout();
	}

	// fire the current weapon, sending the client-computed aim point (camera
	// ray end incl. pitch) so the server spawns projectiles in the right direction
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StartFiring(GetWeaponTargetLocation());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s DoStartFiring - no weapon equipped (CurrentWeapon=%s, owned=%d, loadoutOpen=%d)"),
			*GetName(), *GetNameSafe(CurrentWeapon), OwnedWeapons.Num(), bLoadoutOpen ? 1 : 0);
	}
}

void AShooterCharacter::DoStopFiring()
{
	// stop firing the current weapon
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StopFiring();
	}
}

void AShooterCharacter::DoSwitchWeapon()
{
	// Routing only: the actual switch is server-authoritative so every client
	// agrees on the equipped weapon (CurrentWeapon is replicated).
	if (IsDead())
	{
		return;
	}

	if (HasAuthority())
	{
		SwitchWeaponNext();
	}
	else
	{
		ServerSwitchWeapon();
	}
}

void AShooterCharacter::ServerSwitchWeapon_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}
	SwitchWeaponNext();
}

void AShooterCharacter::SwitchWeaponNext()
{
	// ensure we have at least two weapons two switch between
	if (OwnedWeapons.Num() > 1 && !IsDead())
	{
		// deactivate the old weapon
		CurrentWeapon->DeactivateWeapon();

		// find the index of the current weapon in the owned list
		int32 WeaponIndex = OwnedWeapons.Find(CurrentWeapon);

		// is this the last weapon?
		if (WeaponIndex == OwnedWeapons.Num() - 1)
		{
			// loop back to the beginning of the array
			WeaponIndex = 0;
		}
		else {
			// select the next weapon index
			++WeaponIndex;
		}

		// set the new weapon as current
		CurrentWeapon = OwnedWeapons[WeaponIndex];

		// activate the new weapon
		CurrentWeapon->ActivateWeapon(PlayerTag);
	}
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterCharacter, CurrentHP);
	DOREPLIFETIME(AShooterCharacter, CurrentWeapon);
	DOREPLIFETIME(AShooterCharacter, TeamByte);
}

void AShooterCharacter::OnRep_CurrentHP()
{
	// Refresh the health bar on the owning client (server authority decided
	// the value); other clients have no HUD for this pawn.
	BroadcastHealthRatio(FMath::Clamp(CurrentHP / MaxHP, 0.0f, 1.0f));
}

void AShooterCharacter::OnRep_Team()
{
	// Recolor locally — dynamic material instances are NOT replicated, so the
	// client must apply the team tint itself when the replicated team arrives.
	ApplyTeamColor(TeamByte);
}

void AShooterCharacter::OnRep_CurrentWeapon()
{
	UE_LOG(LogTemp, Warning, TEXT("AMMO_UI: %s OnRep_CurrentWeapon weapon=%s"),
		*GetName(), *GetNameSafe(CurrentWeapon));

	// The equipped weapon changed (server authority): refresh the ammo HUD.
	if (CurrentWeapon)
	{
		OnWeaponActivated(CurrentWeapon);
	}
}

void AShooterCharacter::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	Weapon->AttachToActor(this, AttachmentRule);

	// attach the weapon meshes
	Weapon->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	Weapon->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, FirstPersonWeaponSocket);
	
}

void AShooterCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
	// stub
}

void AShooterCharacter::AddWeaponRecoil(float Recoil)
{
	// apply the recoil as pitch input
	AddControllerPitchInput(Recoil);
}

void AShooterCharacter::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	UE_LOG(LogTemp, Warning, TEXT("AMMO_UI: %s UpdateWeaponHUD ammo=%d mag=%d controlled=%d"),
		*GetName(), CurrentAmmo, MagazineSize, IsLocallyControlled() ? 1 : 0);
	BroadcastAmmo(MagazineSize, CurrentAmmo);
}

void AShooterCharacter::BroadcastHealthRatio(float Ratio)
{
	UE_LOG(LogTemp, Warning, TEXT("HP_UI: %s BroadcastHealthRatio ratio=%.2f controlled=%d"),
		*GetName(), Ratio, IsLocallyControlled() ? 1 : 0);
	OnDamaged.Broadcast(Ratio);
}

void AShooterCharacter::BroadcastAmmo(int32 Magazine, int32 Current)
{
	UE_LOG(LogTemp, Warning, TEXT("AMMO_UI: %s BroadcastAmmo mag=%d cur=%d controlled=%d"),
		*GetName(), Magazine, Current, IsLocallyControlled() ? 1 : 0);
	OnBulletCountUpdated.Broadcast(Magazine, Current);
}

FVector AShooterCharacter::GetWeaponTargetLocation()
{
	// trace ahead from the camera viewpoint
	FHitResult OutHit;

	const FVector Start = GetFirstPersonCameraComponent()->GetComponentLocation();
	const FVector End = Start + (GetFirstPersonCameraComponent()->GetForwardVector() * MaxAimDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	// do we already own this weapon?
	AShooterWeapon* OwnedWeapon = FindWeaponOfType(WeaponClass);

	if (!OwnedWeapon)
	{
		// spawn the new weapon
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

		AShooterWeapon* AddedWeapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);

		if (AddedWeapon)
		{
			// add the weapon to the owned list
			OwnedWeapons.Add(AddedWeapon);

			// if we have an existing weapon, deactivate it
			if (CurrentWeapon)
			{
				CurrentWeapon->DeactivateWeapon();
			}

			// switch to the new weapon
			CurrentWeapon = AddedWeapon;
			CurrentWeapon->ActivateWeapon(PlayerTag);
		}
	}
}

void AShooterCharacter::OnWeaponActivated(AShooterWeapon* Weapon)
{
	// update the bullet counter (owning client only)
	BroadcastAmmo(Weapon->GetMagazineSize(), Weapon->GetBulletCount());

	// set the character mesh AnimInstances
	GetFirstPersonMesh()->SetAnimInstanceClass(Weapon->GetFirstPersonAnimInstanceClass());
	GetMesh()->SetAnimInstanceClass(Weapon->GetThirdPersonAnimInstanceClass());
}

void AShooterCharacter::OnWeaponDeactivated(AShooterWeapon* Weapon)
{
	// unused
}

void AShooterCharacter::OnSemiWeaponRefire()
{
	// unused
}

AShooterWeapon* AShooterCharacter::FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	// check each owned weapon
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (Weapon->IsA(WeaponClass))
		{
			return Weapon;
		}
	}

	// weapon not found
	return nullptr;

}

void AShooterCharacter::Die(AController* Killer)
{
	// deactivate the weapon
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	// Resolve the killer's team so the point goes to the *killer's* team, not the victim's.
	// Suicide / environment damage (no instigator) or a team-kill awards no point.
	uint8 KillerTeam = 255;
	if (Killer)
	{
		if (const AShooterCharacter* KillerChar = Cast<AShooterCharacter>(Killer->GetPawn()))
		{
			KillerTeam = KillerChar->GetTeamByte();
		}
		else if (const AShooterNPC* KillerNPC = Cast<AShooterNPC>(Killer->GetPawn()))
		{
			KillerTeam = KillerNPC->GetTeamByte();
		}
	}

	if (KillerTeam != 255 && KillerTeam != TeamByte)
	{
		if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
		{
			GM->ReportKill(KillerTeam);
		}
	}

	// grant the death tag to the character
	Tags.Add(DeathTag);
		
	// stop character movement (server-authoritative)
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

	// disable collision (server-authoritative)
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// sync the death presentation (input disable / HUD reset / BP_OnDeath) to all clients
	Multicast_OnDeath();

	// Requirement: the SAME pawn is reused for revival — no destroy/respawn.
	// Count down RespawnTime (2s), then Revive() restores this pawn in place.
	GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterCharacter::Revive, RespawnTime, false);
}

void AShooterCharacter::Multicast_OnDeath_Implementation()
{
	// Presentation only — gameplay state was already handled on the server.
	// Disable controls (client-side; the server never routes input to a dead pawn).
	DisableInput(nullptr);

	// reset the bullet counter UI (owning client only)
	BroadcastAmmo(0, 0);

	// call the BP handler (death anim / effects)
	BP_OnDeath();
}

void AShooterCharacter::OnRespawn()
{
	// Legacy path kept for safety — the TDM flow uses Revive() (pawn reuse).
	Destroy();
}

void AShooterCharacter::Revive()
{
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s Revive - restoring pawn in place"), *GetName());

	// Teleport back to a team spawn point (PlayerStart 0-4 = RED, 5-8 = BLUE).
	if (AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
	{
		if (AActor* Start = TDM->FindTeamPlayerStart(TeamByte))
		{
			SetActorLocationAndRotation(Start->GetActorLocation(), Start->GetActorRotation());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s Revive - no spawn point found for team %u, staying in place"), *GetName(), TeamByte);
		}
	}

	// Restore the character (same pawn reused — no new spawn).
	CurrentHP = MaxHP;
	Tags.Remove(DeathTag);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetDefaultMovementMode();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetActorHiddenInGame(false);

	// Re-equip the weapon (deactivated on death).
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->ActivateWeapon(PlayerTag);
	}

	// Requirement: spawn protection on revive too.
	float Protection = 3.0f;
	if (const AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
	{
		Protection = TDM->GetSpawnProtectionTime();
	}
	GrantSpawnProtection(Protection);

	// Broadcast the revive presentation to all clients.
	Multicast_OnRevive();
}

void AShooterCharacter::Multicast_OnRevive_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s Multicast_OnRevive"), *GetName());

	// Presentation only — gameplay state was already restored on the server.
	EnableInput(nullptr);

	// Restore anything a death Blueprint may have hidden/disabled.
	SetActorHiddenInGame(false);
	GetMesh()->SetHiddenInGame(false);
	GetMesh()->SetVisibility(true, true);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// Stop any death montage still playing so the AnimBP returns to its
	// locomotion state (the Blueprint BP_OnRevive can do the full reset).
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->StopAllMontages(0.0f);
	}

	// Hard-undo ragdoll on the third-person mesh: reattach to the capsule,
	// zero all physics velocities and restore the spawn pose/profile. This
	// prevents the pawn from coming back lying sideways and sliding on the
	// floor even if the death Blueprint's ragdoll wasn't fully reverted.
	if (GetMesh()->IsSimulatingPhysics())
	{
		GetMesh()->SetSimulatePhysics(false);
	}
	GetMesh()->SetCollisionProfileName(InitialMeshCollisionProfile);
	GetMesh()->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
	GetMesh()->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	GetMesh()->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	GetMesh()->SetRelativeLocationAndRotation(InitialMeshTransform.GetLocation(), InitialMeshTransform.GetRotation());

	// Force the standard first-person view (kills any Death Camera / SpringArm
	// the death Blueprint left active).
	ForceFirstPersonView();

	BP_OnRevive();

	// Re-assert the first-person view one frame later: the Blueprint
	// BP_OnRevive above may have re-applied its own attach / view settings
	// (SavedViewOffset etc.), which would otherwise override the revive.
	GetWorld()->GetTimerManager().SetTimerForNextTick([this]()
	{
		if (IsActorBeingDestroyed())
		{
			return;
		}
		ForceFirstPersonView();
	});

	// Refresh the HUD (health full + ammo of the re-equipped weapon) — owning
	// client only, the same way the death/replication paths do.
	BroadcastHealthRatio(1.0f);
	BroadcastAmmo(
		IsValid(CurrentWeapon) ? CurrentWeapon->GetMagazineSize() : 0,
		IsValid(CurrentWeapon) ? CurrentWeapon->GetBulletCount() : 0);
}

void AShooterCharacter::ForceFirstPersonView()
{
	// Deactivate every camera component that is not the first-person camera.
	// (A SpringArm's built-in camera IS a UCameraComponent, so this covers it.)
	TArray<UCameraComponent*> Cameras;
	GetComponents<UCameraComponent>(Cameras);
	for (UCameraComponent* Cam : Cameras)
	{
		if (Cam != GetFirstPersonCameraComponent())
		{
			Cam->SetActive(false);
		}
	}

	// Restore the first-person rig to its spawn pose (in case the death
	// Blueprint moved/attached things around).
	GetFirstPersonMesh()->SetRelativeTransform(InitialFPMeshTransform);
	GetFirstPersonCameraComponent()->SetRelativeTransform(InitialFPCameraTransform);
	GetFirstPersonCameraComponent()->SetActive(true);

	if (AController* Ctrl = GetController())
	{
		if (APlayerController* PC = Cast<APlayerController>(Ctrl))
		{
			PC->SetViewTarget(this);
		}
	}
}

bool AShooterCharacter::IsDead() const
{
	// the character is dead if their current HP drops to zero
	return CurrentHP <= 0.0f;
}

void AShooterCharacter::SetTeam(uint8 Team)
{
	TeamByte = Team;

	// Recolor the third-person mesh to the team color (RED / BLUE).
	ApplyTeamColor(Team);
}

void AShooterCharacter::GrantSpawnProtection(float Duration)
{
	bIsSpawnProtected = true;

	// (Re)start the timer that clears protection after Duration seconds.
	GetWorld()->GetTimerManager().ClearTimer(SpawnProtectionTimer);
	GetWorld()->GetTimerManager().SetTimer(
		SpawnProtectionTimer, this, &AShooterCharacter::ClearSpawnProtection,
		FMath::Max(0.0f, Duration), false);
}

void AShooterCharacter::ClearSpawnProtection()
{
	bIsSpawnProtected = false;
	GetWorld()->GetTimerManager().ClearTimer(SpawnProtectionTimer);
}

void AShooterCharacter::OpenLoadout()
{
	// Default the pending weapon to the TDM game mode's first loadout entry so the
	// player always spawns with a usable gun even if they never touch the panel.
	if (!PendingLoadoutWeapon)
	{
		if (const AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
		{
			const TArray<TSubclassOf<AShooterWeapon>>& Loadout = TDM->GetLoadoutWeapons();
			if (Loadout.Num() > 0)
			{
				PendingLoadoutWeapon = Loadout[0];
			}
		}
		// Guarantee a gun when the loadout list is empty (unconfigured TDM).
		if (!PendingLoadoutWeapon)
		{
			PendingLoadoutWeapon = DefaultWeaponClass;
			UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s OpenLoadout - loadout list empty, fallback weapon = %s"),
				*GetName(), *GetNameSafe(DefaultWeaponClass));
		}
	}

	bLoadoutOpen = true;
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s OpenLoadout - bLoadoutOpen=true, pending=%s"),
		*GetName(), *GetNameSafe(PendingLoadoutWeapon));
}

void AShooterCharacter::CloseLoadout()
{
	if (!bLoadoutOpen)
	{
		return;
	}

	bLoadoutOpen = false;

	// Equip the pre-selected weapon (falls back to the default from OpenLoadout).
	if (PendingLoadoutWeapon)
	{
		AddWeaponClass(PendingLoadoutWeapon);
		UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: %s CloseLoadout - equipped %s (owned=%d)"),
			*GetName(), *PendingLoadoutWeapon->GetName(), OwnedWeapons.Num());
	}

	// Tell the owning player controller to hide the loadout widget.
	if (AShooterPlayerController* PC = Cast<AShooterPlayerController>(GetController()))
	{
		PC->HideLoadoutWidget();
	}
}

void AShooterCharacter::SetPendingLoadoutWeapon(TSubclassOf<AShooterWeapon> Weapon)
{
	if (bLoadoutOpen && Weapon)
	{
		PendingLoadoutWeapon = Weapon;
	}
}
