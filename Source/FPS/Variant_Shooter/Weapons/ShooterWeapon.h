// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShooterWeaponHolder.h"
#include "Animation/AnimInstance.h"
#include "ShooterWeapon.generated.h"

class IShooterWeaponHolder;
class AShooterProjectile;
class USkeletalMeshComponent;
class UAnimMontage;
class UAnimInstance;

/**
 *  Base class for a simple first person shooter weapon
 *  Provides both first person and third person perspective meshes
 *  Handles ammo and firing logic
 *  Interacts with the weapon owner through the ShooterWeaponHolder interface
 */
UCLASS(abstract)
class FPS_API AShooterWeapon : public AActor
{
	GENERATED_BODY()
	
	/** First person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** Third person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* ThirdPersonMesh;

protected:

	/** Cast pointer to the weapon owner */
	IShooterWeaponHolder* WeaponOwner;

	/** Type of projectiles this weapon will shoot */
	UPROPERTY(EditAnywhere, Category="Ammo")
	TSubclassOf<AShooterProjectile> ProjectileClass;

	/** Number of bullets in a magazine */
	UPROPERTY(EditAnywhere, Category="Ammo", meta = (ClampMin = 0, ClampMax = 100))
	int32 MagazineSize = 10;

	/** Number of bullets in the current magazine (replicated — the server is
	 *  the ammo authority; every client's HUD reacts to OnRep_CurrentBullets). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentBullets, Category="Ammo")
	int32 CurrentBullets = 0;

	/** Client-side handler: fires the local "bullets changed" event (HUD
	 *  refresh) whenever the server's authoritative count arrives. */
	UFUNCTION()
	void OnRep_CurrentBullets();
	
	/** Animation montage to play when firing this weapon */
	UPROPERTY(EditAnywhere, Category="Animation")
	UAnimMontage* FiringMontage;

	/** AnimInstance class to set for the first person character mesh when this weapon is active */
	UPROPERTY(EditAnywhere, Category="Animation")
	TSubclassOf<UAnimInstance> FirstPersonAnimInstanceClass;

	/** AnimInstance class to set for the third person character mesh when this weapon is active */
	UPROPERTY(EditAnywhere, Category="Animation")
	TSubclassOf<UAnimInstance> ThirdPersonAnimInstanceClass;

	/** Cone half-angle for variance while aiming */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 90, Units = "Degrees"))
	float AimVariance = 0.0f;

	/** Amount of firing recoil to apply to the owner */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 100))
	float FiringRecoil = 0.0f;

	/** Name of the first person muzzle socket where projectiles will spawn */
	UPROPERTY(EditAnywhere, Category="Aim")
	FName MuzzleSocketName;

	/** Distance ahead of the muzzle that bullets will spawn at */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm"))
	float MuzzleOffset = 10.0f;

	/** If true, this weapon will automatically fire at the refire rate */
	UPROPERTY(EditAnywhere, Category="Refire")
	bool bFullAuto = false;

	/** Time between shots for this weapon. Affects both full auto and semi auto modes */
	UPROPERTY(EditAnywhere, Category="Refire", meta = (ClampMin = 0, ClampMax = 5, Units = "s"))
	float RefireRate = 0.5f;

	/** How long reloading takes once the magazine is empty (defaults by weapon
	 *  type: 0.5s pistol / 0.8s rifle / 1.0s grenade launcher; override in the
	 *  Blueprint). During reload the weapon cannot fire. */
	UPROPERTY(EditAnywhere, Category="Ammo", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float ReloadTime = 0.5f;

	/** True while the magazine is being refilled (blocks firing). Replicated so
	 *  every client's HUD can show a "reloading" indicator. Server-only writes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_IsReloading, Category="Ammo")
	bool bIsReloading = false;

	/** Client-side handler: fires the reload-state HUD event when the
	 *  replicated value changes. */
	UFUNCTION()
	void OnRep_IsReloading();

	/** Broadcasts the current reload state to the owner character (called on
	 *  the server when the state changes, and on clients via OnRep). */
	void NotifyReloadState();

	/** Timer that finishes the reload after ReloadTime. */
	FTimerHandle ReloadTimer;

	/** Game time of last shot fired, used to enforce refire rate on semi auto */
	float TimeOfLastShot = 0.0f;

	/** If true, the weapon is currently firing */
	bool bIsFiring = false;

	/** Timer to handle full auto refiring */
	FTimerHandle RefireTimer;

	/** One-shot timer that pushes the initial ammo count to the owning client's
	 *  HUD once the weapon is fully replicated (the replicated initial ammo
	 *  equals the locally-set magazine size, so OnRep_CurrentBullets does NOT
	 *  fire on spawn and the HUD would stay blank until the first shot). */
	FTimerHandle InitialAmmoReportTimer;

	/** Cast pawn pointer to the owner for AI perception system interactions */
	TObjectPtr<APawn> PawnOwner;

	/** Loudness of the shot for AI perception system interactions */
	UPROPERTY(EditAnywhere, Category="Perception", meta = (ClampMin = 0, ClampMax = 100))
	float ShotLoudness = 1.0f;

	/** Max range of shot AI perception noise */
	UPROPERTY(EditAnywhere, Category="Perception", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float ShotNoiseRange = 300.0f;

	/** Tag to apply to noise generated by shooting this weapon */
	UPROPERTY(EditAnywhere, Category="Perception")
	FName NoiseOwnerTag = FName("Shot");

	/** True once the weapon meshes are attached to the owner (client-side guard:
	 *  attachment is local per machine, retried from Tick if the owner's mesh
	 *  wasn't ready during BeginPlay). */
	bool bMeshesAttached = false;

public:	

	/** Constructor */
	AShooterWeapon();

protected:
	
	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay Cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

protected:

	/** Called when the weapon's owner is destroyed */
	UFUNCTION()
	void OnOwnerDestroyed(AActor* DestroyedActor);

public:

	/** Activates this weapon and gets it ready to fire */
	void ActivateWeapon(const FName& OwnerTag);

	/** Deactivates this weapon */
	void DeactivateWeapon();

	/** Start firing this weapon. AimTarget is the client-computed aim point
	 *  (camera ray end) so the server spawns projectiles with the correct pitch;
	 *  MuzzleWorld is the client-observed muzzle position (its FP mesh socket is
	 *  accurate locally) so bullets leave from the gun barrel on the server too.
	 *  Zero vectors let the server compute both (e.g. AI bots). */
	void StartFiring(const FVector& AimTarget = FVector::ZeroVector, const FVector& MuzzleWorld = FVector::ZeroVector);

	/** Stop firing this weapon */
	void StopFiring();

	/** Returns the world-space muzzle position of this weapon's first-person
	 *  mesh (accurate on the owning client; falls back to actor location). */
	FVector GetMuzzleWorldLocation() const;

protected:

	/** Server-authoritative fire logic (spawns the projectile on the server). */
	virtual void Fire();

	/** Called when the refire rate time has passed while shooting semi auto weapons */
	void FireCooldownExpired();

	/** Fire a projectile from StartLocation towards TargetLocation (server-only). */
	virtual void FireProjectile(const FVector& TargetLocation, const FVector& StartLocation);

	/** Calculates the spawn transform for projectiles shot by this weapon */
	FTransform CalculateProjectileSpawnTransform(const FVector& TargetLocation, const FVector& StartLocation) const;

	/** Server RPC: client asks the server to start firing, sending aim + muzzle. */
	UFUNCTION(Server, Reliable)
	void ServerStartFiring(FVector AimTarget, FVector MuzzleWorld);

	/** Server RPC: client asks the server to stop firing. */
	UFUNCTION(Server, Reliable)
	void ServerStopFiring();

	/** Plays the firing feedback on every client (montage / recoil / ammo HUD). */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnFire();

	/** Fires now or schedules the next shot (called on the server). */
	void StartFiringInternal();

	/** Server-only: starts the delayed magazine refill once the clip is empty. */
	void StartReload();

	/** Server-only: refills the magazine (replicated to clients). */
	void FinishReload();

	/** Aim point last received from the owning client (used by server Fire). */
	FVector PendingAimTarget = FVector::ZeroVector;

	/** Muzzle position last received from the owning client (used by server Fire). */
	FVector PendingMuzzleLocation = FVector::ZeroVector;

	/** Game time of the last aim update sent to the server (throttle). */
	float LastAimReportTime = 0.0f;

	/** Per-frame: while firing, the client keeps streaming its latest aim point
	 *  to the server so sustained fire follows the crosshair. */
	virtual void Tick(float DeltaTime) override;

	/** Server RPC (unreliable, throttled): refresh the aim point while firing. */
	UFUNCTION(Server, Unreliable)
	void ServerUpdateAim(FVector AimTarget, FVector MuzzleWorld);

public:

	/** Returns the first person mesh */
	UFUNCTION(BlueprintPure, Category="Weapon")
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; };

	/** Returns the third person mesh */
	UFUNCTION(BlueprintPure, Category="Weapon")
	USkeletalMeshComponent* GetThirdPersonMesh() const { return ThirdPersonMesh; };

	/** Returns the first person anim instance class */
	const TSubclassOf<UAnimInstance>& GetFirstPersonAnimInstanceClass() const;

	/** Returns the third person anim instance class */
	const TSubclassOf<UAnimInstance>& GetThirdPersonAnimInstanceClass() const;

	/** Returns the magazine size */
	int32 GetMagazineSize() const { return MagazineSize; };

	/** Returns the current bullet count */
	int32 GetBulletCount() const { return CurrentBullets; }

	/** Returns the time between shots (used by NPC auto-fire). */
	float GetRefireRate() const { return RefireRate; }

	/** Registers the replicated ammo count. */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
