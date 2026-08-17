// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSCharacter.h"
#include "ShooterWeaponHolder.h"
#include "ShooterNPC.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPawnDeathDelegate);

class AShooterWeapon;

/**
 *  A simple AI-controlled shooter game NPC
 *  Executes its behavior through a StateTree managed by its AI Controller
 *  Holds and manages a weapon
 */
UCLASS(abstract)
class FPS_API AShooterNPC : public AFPSCharacter, public IShooterWeaponHolder
{
	GENERATED_BODY()

public:

	/** Current HP for this character. It dies if it reaches zero through damage */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	float CurrentHP = 100.0f;

protected:

	/** Name of the collision profile to use during ragdoll death */
	UPROPERTY(EditAnywhere, Category="Damage")
	FName RagdollCollisionProfile = FName("Ragdoll");

	/** Time to wait after death before destroying this actor */
	UPROPERTY(EditAnywhere, Category="Damage")
	float DeferredDestructionTime = 5.0f;

	/** Team byte for this character (replicated so every client recolors the
	 *  mesh locally — materials are not replicated).
	 *  Default 255 (unassigned) so the FIRST replicated value (0 or 1) always
	 *  differs from the CDO and reliably triggers OnRep_Team on clients. */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Team, Category="Team")
	uint8 TeamByte = 255;

	/** Actor tag to grant this character when it dies */
	UPROPERTY(EditAnywhere, Category="Team")
	FName DeathTag = FName("Dead");

	/** Pointer to the equipped weapon */
	TObjectPtr<AShooterWeapon> Weapon;

	/** Type of weapon to spawn for this character */
	UPROPERTY(EditAnywhere, Category="Weapon")
	TSubclassOf<AShooterWeapon> WeaponClass;

	/** Name of the first person mesh weapon socket */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category ="Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	/** Name of the third person mesh weapon socket */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category ="Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/** Max range for aiming calculations */
	UPROPERTY(EditAnywhere, Category="Aim")
	float AimRange = 10000.0f;

	/** Cone variance to apply while aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float AimVarianceHalfAngle = 15.0f;

	/** Minimum vertical offset from the target center to apply when aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float MinAimOffsetZ = -35.0f;

	/** Maximum vertical offset from the target center to apply when aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float MaxAimOffsetZ = -60.0f;

	/** Actor currently being targeted */
	TObjectPtr<AActor> CurrentAimTarget;

	/** If true, this character is currently shooting its weapon */
	bool bIsShooting = false;

	/** If true, this character has already died */
	bool bIsDead = false;

	/** True while the character is spawn-protected (TDM invincibility on respawn) */
	bool bIsSpawnProtected = false;

	/** Timer that clears spawn protection after its duration. */
	FTimerHandle SpawnProtectionTimer;

	/** Auto-fire timer: keeps firing at the weapon's refire rate while a target
	 *  is held (the pistol is semi-auto, so StartFiring alone only fires once). */
	FTimerHandle AutoFireTimer;

	/** Auto-fire tick called by AutoFireTimer. */
	void AutoFireTick();

	/** Deferred destruction on death timer (reused for the revive countdown). */
	FTimerHandle DeathTimer;

	/** Mesh relative transform captured at spawn (restored on revive). */
	FTransform InitialMeshTransform;

	/** Mesh collision profile captured at spawn (restored on revive). */
	FName InitialMeshCollisionProfile;

public:

	/** Delegate called when this NPC dies (legacy — TDM now reuses the pawn). */
	FPawnDeathDelegate OnPawnDeath;

	/** Returns the team ID for this character */
	uint8 GetTeamByte() const { return TeamByte; }

	/** Sets the team ID for this character (used by the TDM spawner to fill teams) */
	void SetTeam(uint8 Team);

	/** Returns true if the character is dead */
	bool IsDead() const { return bIsDead; }

	/** Grants SpawnProtectionTime seconds of invincibility. */
	void GrantSpawnProtection(float Duration);

	/** Clears spawn protection immediately. */
	void ClearSpawnProtection();

	/** Returns true while the character is spawn-protected (invincible). */
	bool IsSpawnProtected() const { return bIsSpawnProtected; }

	/** Server-only: revives this SAME pawn after the death countdown (reuse). */
	void Revive();

	/** Server->All: revive presentation (undo ragdoll, restore visibility). */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnRevive();

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay cleanup */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

	/** Handle incoming damage */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Replication (TeamByte so clients recolor the bot locally). */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Called on clients when the replicated team changes. */
	UFUNCTION()
	void OnRep_Team();

public:

	//~Begin IShooterWeaponHolder interface

	/** Attaches a weapon's meshes to the owner */
	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;

	/** Plays the firing montage for the weapon */
	virtual void PlayFiringMontage(UAnimMontage* Montage) override;

	/** Applies weapon recoil to the owner */
	virtual void AddWeaponRecoil(float Recoil) override;

	/** Updates the weapon's HUD with the current ammo count */
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) override;

	/** Calculates and returns the aim location for the weapon */
	virtual FVector GetWeaponTargetLocation() override;

	/** Gives a weapon of this class to the owner */
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

	/** Activates the passed weapon */
	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override;

	/** Deactivates the passed weapon */
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override;

	/** Notifies the owner that the weapon cooldown has expired and it's ready to shoot again */
	virtual void OnSemiWeaponRefire() override;

	//~End IShooterWeaponHolder interface

protected:

	/** Called when HP is depleted and the character should die */
	void Die(AController* Killer);

	/** Called after death to destroy the actor */
	void DeferredDestruction();

	/** Server->All: death presentation (ragdoll physics) on every client.
	 *  Gameplay death (scoring, OnPawnDeath, destruction) is server-authoritative. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnDeath();

public:

	/** Signals this character to start shooting at the passed actor */
	void StartShooting(AActor* ActorToShoot);

	/** Signals this character to stop shooting */
	void StopShooting();
};
