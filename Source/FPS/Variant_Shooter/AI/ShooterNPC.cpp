// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterNPC.h"
#include "ShooterWeapon.h"
#include "ShooterCharacter.h"
#include "ShooterTDMGameMode.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "ShooterGameMode.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Animation/AnimInstance.h"

void AShooterNPC::BeginPlay()
{
	Super::BeginPlay();

	// Remember the mesh pose/profile so revive can undo the ragdoll on every machine.
	InitialMeshTransform = GetMesh()->GetRelativeTransform();
	InitialMeshCollisionProfile = GetMesh()->GetCollisionProfileName();

	// Only the server runs gameplay setup (spawn protection + weapon spawn).
	// Clients just render the replicated actor and its replicated weapon.
	if (!HasAuthority())
	{
		return;
	}

	// Requirement: spawn protection is a design requirement — every freshly
	// spawned unit (players AND bots) is invincible for a short window to
	// prevent spawn-camping. See TDM_需求文档.md.
	if (const AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GrantSpawnProtection(TDM->GetSpawnProtectionTime());
	}

	// spawn the weapon
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Weapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);
}

void AShooterNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the death timer
	GetWorld()->GetTimerManager().ClearTimer(DeathTimer);
	GetWorld()->GetTimerManager().ClearTimer(AutoFireTimer);
}

float AShooterNPC::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Server-authoritative: damage is only applied on the server, so every
	// client agrees on when/where an NPC dies (DS-synced death).
	if (!HasAuthority())
	{
		return 0.0f;
	}

	// Requirement: during the PREPARE phase all damage is ignored (bots don't
	// even exist yet, but this guards any early edge case).
	if (const AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
	{
		if (TDM->IsPreparePhase())
		{
			return 0.0f;
		}
	}

	// ignore if already dead
	if (bIsDead)
	{
		return 0.0f;
	}

	// ignore damage while spawn-protected (TDM invincibility window)
	if (bIsSpawnProtected)
	{
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

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		Die(EventInstigator);
	}

	return Damage;
}

void AShooterNPC::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	WeaponToAttach->AttachToActor(this, AttachmentRule);

	// attach the weapon meshes
	WeaponToAttach->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	WeaponToAttach->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, ThirdPersonWeaponSocket);
}

void AShooterNPC::PlayFiringMontage(UAnimMontage* Montage)
{
	// unused
}

void AShooterNPC::AddWeaponRecoil(float Recoil)
{
	// unused
}

void AShooterNPC::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	// unused
}

FVector AShooterNPC::GetWeaponTargetLocation()
{
	// start aiming from the camera location
	const FVector AimSource = GetFirstPersonCameraComponent()->GetComponentLocation();

	FVector AimDir, AimTarget = FVector::ZeroVector;

	// do we have an aim target?
	if (CurrentAimTarget)
	{
		// target the actor location
		AimTarget = CurrentAimTarget->GetActorLocation();

		// apply a vertical offset to target head/feet
		AimTarget.Z += FMath::RandRange(MinAimOffsetZ, MaxAimOffsetZ);

		// get the aim direction and apply randomness in a cone
		AimDir = (AimTarget - AimSource).GetSafeNormal();
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(AimDir, AimVarianceHalfAngle);

		
	} else {

		// no aim target, so just use the camera facing
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(GetFirstPersonCameraComponent()->GetForwardVector(), AimVarianceHalfAngle);

	}

	// calculate the unobstructed aim target location
	AimTarget = AimSource + (AimDir * AimRange);

	// run a visibility trace to see if there's obstructions
	FHitResult OutHit;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, AimSource, AimTarget, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterNPC::AddWeaponClass(const TSubclassOf<AShooterWeapon>& InWeaponClass)
{
	// unused
}

void AShooterNPC::OnWeaponActivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnWeaponDeactivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnSemiWeaponRefire()
{
	// are we still shooting?
	if (bIsShooting)
	{
		// fire the weapon
		Weapon->StartFiring();
	}
}

void AShooterNPC::Die(AController* Killer)
{
	// ignore if already dead
	if (bIsDead)
	{
		return;
	}

	// raise the dead flag
	bIsDead = true;

	// stop firing (a dead bot must not keep shooting)
	StopShooting();

	// Resolve the killer's team so the point goes to the *killer's* team, not the victim's.
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

	// grant the death tag to the character
	Tags.Add(DeathTag);

	// Requirement: bots reuse their pawn on respawn (no destroy + rebuild),
	// matching the player flow. The roster is kept full by Revive(); a safety
	// timer on the game mode covers the rare case of a bot being destroyed.

	// Award the point to the killer's team (ignore team-kills / suicides).
	if (KillerTeam != 255 && KillerTeam != TeamByte)
	{
		if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
		{
			GM->ReportKill(KillerTeam);
		}
	}

	// disable capsule collision
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// stop movement
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->StopActiveMovement();

	// sync the ragdoll presentation to every client
	Multicast_OnDeath();

	// Revive the SAME pawn after a short countdown (requirement: reuse pawn).
	GetWorld()->GetTimerManager().SetTimer(DeathTimer, this, &AShooterNPC::Revive, 2.0f, false);
}

void AShooterNPC::Multicast_OnDeath_Implementation()
{
	// Presentation only — ragdoll on every client. Gameplay state (scoring,
	// OnPawnDeath broadcast, deferred destruction) is server-authoritative.
	GetMesh()->SetCollisionProfileName(RagdollCollisionProfile);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetPhysicsBlendWeight(1.0f);
}

void AShooterNPC::DeferredDestruction()
{
	// Legacy destroy path kept for safety — the TDM flow uses Revive().
	Destroy();
}

void AShooterNPC::Revive()
{
	if (!HasAuthority())
	{
		return;
	}

	// Teleport back to a team spawn point (numbered starts 1-4 = RED, 5-8 = BLUE).
	if (AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
	{
		if (AActor* Start = TDM->FindTeamPlayerStart(TeamByte))
		{
			SetActorLocationAndRotation(Start->GetActorLocation(), Start->GetActorRotation());
		}
	}

	// Restore the character (same pawn reused — no new spawn).
	bIsDead = false;
	CurrentHP = 100.0f;
	Tags.Remove(DeathTag);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetDefaultMovementMode();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetActorHiddenInGame(false);

	// Requirement: spawn protection on revive too.
	if (const AShooterTDMGameMode* TDM = Cast<AShooterTDMGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GrantSpawnProtection(TDM->GetSpawnProtectionTime());
	}

	// Broadcast the revive presentation to all clients.
	Multicast_OnRevive();
}

void AShooterNPC::Multicast_OnRevive_Implementation()
{
	// Undo the ragdoll and restore the mesh to its spawn pose (all machines).
	if (GetMesh()->IsSimulatingPhysics())
	{
		GetMesh()->SetSimulatePhysics(false);
	}
	GetMesh()->SetCollisionProfileName(InitialMeshCollisionProfile);
	GetMesh()->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
	GetMesh()->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	GetMesh()->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	GetMesh()->SetRelativeLocationAndRotation(InitialMeshTransform.GetLocation(), InitialMeshTransform.GetRotation());
	GetMesh()->SetPhysicsBlendWeight(0.0f);

	SetActorHiddenInGame(false);
	GetMesh()->SetHiddenInGame(false);
	GetMesh()->SetVisibility(true, true);

	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->StopAllMontages(0.0f);
	}
}

void AShooterNPC::SetTeam(uint8 Team)
{
	TeamByte = Team;

	// Recolor the third-person mesh to the team color (RED / BLUE).
	ApplyTeamColor(Team);
}

void AShooterNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterNPC, TeamByte);
}

void AShooterNPC::OnRep_Team()
{
	// Recolor locally — dynamic material instances are NOT replicated.
	ApplyTeamColor(TeamByte);
}

void AShooterNPC::GrantSpawnProtection(float Duration)
{
	bIsSpawnProtected = true;

	GetWorld()->GetTimerManager().ClearTimer(SpawnProtectionTimer);
	GetWorld()->GetTimerManager().SetTimer(
		SpawnProtectionTimer, this, &AShooterNPC::ClearSpawnProtection,
		FMath::Max(0.0f, Duration), false);
}

void AShooterNPC::ClearSpawnProtection()
{
	bIsSpawnProtected = false;
	GetWorld()->GetTimerManager().ClearTimer(SpawnProtectionTimer);
}

void AShooterNPC::StartShooting(AActor* ActorToShoot)
{
	// save the aim target
	CurrentAimTarget = ActorToShoot;

	// raise the flag
	bIsShooting = true;

	if (!Weapon)
	{
		return;
	}

	// Fire the first shot immediately, then KEEP firing at the weapon's refire
	// rate while the target is held — the pistol is semi-auto, so a single
	// StartFiring call would only fire one bullet.
	Weapon->StartFiring();
	GetWorldTimerManager().SetTimer(AutoFireTimer, this, &AShooterNPC::AutoFireTick, Weapon->GetRefireRate(), true);
}

void AShooterNPC::AutoFireTick()
{
	// keep firing while we still hold the target and are alive
	if (bIsShooting && Weapon && !IsDead())
	{
		Weapon->StartFiring();
	}
}

void AShooterNPC::StopShooting()
{
	// lower the flag
	bIsShooting = false;

	// stop the auto-fire loop
	GetWorldTimerManager().ClearTimer(AutoFireTimer);

	// signal the weapon
	if (Weapon)
	{
		Weapon->StopFiring();
	}
}
