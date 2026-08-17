// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSCharacter.h"
#include "ShooterWeaponHolder.h"
#include "ShooterCharacter.generated.h"

class AShooterWeapon;
class UInputAction;
class UInputComponent;
class UPawnNoiseEmitterComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBulletCountUpdatedDelegate, int32, MagazineSize, int32, Bullets);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDamagedDelegate, float, LifePercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FReloadStateChangedDelegate, bool, bReloading);

/**
 *  A player controllable first person shooter character
 *  Manages a weapon inventory through the IShooterWeaponHolder interface
 *  Manages health and death
 */
UCLASS(abstract)
class FPS_API AShooterCharacter : public AFPSCharacter, public IShooterWeaponHolder
{
	GENERATED_BODY()
	
	/** AI Noise emitter component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UPawnNoiseEmitterComponent* PawnNoiseEmitter;

protected:

	/** Fire weapon input action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* FireAction;

	/** Switch weapon input action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* SwitchWeaponAction;

	/** Name of the first person mesh weapon socket */
	UPROPERTY(EditAnywhere, Category ="Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	/** Name of the third person mesh weapon socket */
	UPROPERTY(EditAnywhere, Category ="Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/** Max distance to use for aim traces */
	UPROPERTY(EditAnywhere, Category ="Aim", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float MaxAimDistance = 10000.0f;

	/** Max HP this character can have */
	/** Maximum HP (kept equal to the AI bots so players fight at the same
	 *  time-to-kill: damage 25 → 4 shots. See TDM_需求文档.md §5). */
	UPROPERTY(EditAnywhere, Category="Health")
	float MaxHP = 100.0f;

	/** Current HP remaining to this character (replicated so clients show the
	 *  same health bar and can gate firing on the real server state). */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_CurrentHP, Category="Health")
	float CurrentHP = 0.0f;

	/** Team ID for this character (replicated so every client recolors the
	 *  mesh locally — materials are not replicated).
	 *  Default 255 (unassigned) so the FIRST replicated value (0 or 1) always
	 *  differs from the CDO and reliably triggers OnRep_Team on clients. */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Team, Category="Team")
	uint8 TeamByte = 255;

	/** Actor tag to grant this character when it dies */
	UPROPERTY(EditAnywhere, Category="Team")
	FName DeathTag = FName("Dead");

	/** Tag to pass to weapons and projectiles to identify their AI perception noise as player-generated */
	UPROPERTY(EditAnywhere, Category="Tags")
	FName PlayerTag = FName("Player");

	/** List of weapons picked up by the character (server-authoritative). */
	TArray<AShooterWeapon*> OwnedWeapons;

	/** Weapon currently equipped and ready to shoot with (replicated so the
	 *  client can route fire input to the right weapon). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentWeapon, Category = "Weapons")
	TObjectPtr<AShooterWeapon> CurrentWeapon;

	/** Revive delay (countdown) before the SAME pawn is reused. */
	UPROPERTY(EditAnywhere, Category ="Destruction", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RespawnTime = 2.0f;

	FTimerHandle RespawnTimer;

	/** Third-person mesh relative transform captured at spawn, restored on
	 *  revive so the pawn doesn't come back lying sideways (ragdoll undo). */
	FTransform InitialMeshTransform;

	/** Third-person mesh collision profile captured at spawn, restored on revive. */
	FName InitialMeshCollisionProfile;

	/** First-person camera relative transform captured at spawn (restored on revive). */
	FTransform InitialFPCameraTransform;

	/** First-person mesh relative transform captured at spawn (restored on revive). */
	FTransform InitialFPMeshTransform;

	//~Begin Spawn protection (TDM invincibility on respawn)
	/** True while the character is spawn-protected (takes no damage). */
	bool bIsSpawnProtected = false;

	/** Timer that clears spawn protection after its duration. */
	FTimerHandle SpawnProtectionTimer;
	//~End Spawn protection

	//~Begin Respawn loadout (TDM weapon pick)
	/** True while the respawn loadout panel is open. */
	bool bLoadoutOpen = false;

	/** Weapon the player has pre-selected in the loadout panel (equipped on close). */
	TSubclassOf<AShooterWeapon> PendingLoadoutWeapon;

	/** Fallback weapon equipped when the TDM loadout list is empty / no panel is
	 *  configured, so a player always has a usable gun. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapons")
	TSubclassOf<AShooterWeapon> DefaultWeaponClass;
	//~End Respawn loadout


public:

	/** Bullet count updated delegate */
	FBulletCountUpdatedDelegate OnBulletCountUpdated;

	/** Damaged delegate */
	FDamagedDelegate OnDamaged;

	/** Reload state changed delegate (fires when the equipped weapon's
	 *  replicated bIsReloading changes — HUD shows a "reloading" indicator). */
	UPROPERTY(BlueprintAssignable, Category="Shooter|HUD")
	FReloadStateChangedDelegate OnReloadStateChanged;

public:

	/** Constructor */
	AShooterCharacter();

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

public:

	/** Handle incoming damage */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

public:

	/** Handles aim inputs from either controls or UI interfaces */
	virtual void DoAim(float Yaw, float Pitch) override;

	/** Handles move inputs from either controls or UI interfaces */
	virtual void DoMove(float Right, float Forward)  override;

	/** Handles jump start inputs from either controls or UI interfaces */
	virtual void DoJumpStart()  override;

	/** Handles jump end inputs from either controls or UI interfaces */
	virtual void DoJumpEnd()  override;

	/** Handles start firing input */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoStartFiring();

	/** Handles stop firing input */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoStopFiring();

	/** Handles switch weapon input */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoSwitchWeapon();

	/** Server RPC: client asks the server to switch to the next weapon. */
	UFUNCTION(Server, Reliable)
	void ServerSwitchWeapon();

	/** Server-side: switches to the next owned weapon (authoritative). */
	void SwitchWeaponNext();

	/** Replication */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Called on clients when the replicated HP changes. */
	UFUNCTION()
	void OnRep_CurrentHP();

	/** Called on clients when the replicated equipped weapon changes. */
	UFUNCTION()
	void OnRep_CurrentWeapon();

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

	/** Returns the currently equipped weapon (if any). */
	AShooterWeapon* GetCurrentWeapon() const { return CurrentWeapon; }

	/** Broadcasts the health-ratio HUD event. Reception is guaranteed to be the
	 *  locally controlled PlayerController: the PC subscribes to its OWN pawn's
	 *  delegates in OnPossess, so remote proxies/servers broadcast into nothing
	 *  and only the owning client's PC receives. (No IsLocallyControlled gate
	 *  here — replication OnReps fire BEFORE possession, which would drop the
	 *  event.) */
	void BroadcastHealthRatio(float Ratio);

	/** Broadcasts the ammo HUD event (same reception guarantee as above). */
	void BroadcastAmmo(int32 Magazine, int32 Current);

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

	/**
	 * Blueprint hook fired on revive (all machines) so the death presentation
	 * set in BP_OnDeath (e.g. death animation state, hidden mesh) can be
	 * reverted. Implement this in BP_ShooterCharacter: switch the AnimBP back
	 * to locomotion, re-show the mesh, etc.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category="Death", meta = (DisplayName = "On Revive"))
	void BP_OnRevive();

protected:

	/** Returns true if the character already owns a weapon of the given class */
	AShooterWeapon* FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const;

	/** Called when this character's HP is depleted */
	void Die(AController* Killer);

	/** Called to allow Blueprint code to react to this character's death */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "On Death"))
	void BP_OnDeath();

	/** Called from the respawn timer to destroy this character and force the PC to respawn */
	void OnRespawn();

	/** Called when the replicated team changes: recolor the mesh on THIS client
	 *  (materials are not replicated, so coloring must run locally). */
	UFUNCTION()
	void OnRep_Team();

	/** Server-only: revives this SAME pawn (reuse, no respawn) after the
	 *  countdown — restores HP/movement/collision and teleports to a spawn. */
	void Revive();

	/** Server->All: revive presentation (re-enable input, refresh HUD). */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnRevive();

	/** Re-forces the standard first-person view: deactivates any leftover
	 *  SpringArm / third-person camera, restores the FP rig pose and sets the
	 *  view target back to this pawn. Used by Multicast_OnRevive directly AND
	 *  one frame later so a Blueprint BP_OnRevive cannot override it. */
	void ForceFirstPersonView();

	/** Server->All: death presentation (input disable, HUD reset, BP_OnDeath).
	 *  Gameplay death (scoring, respawn timer, collision) is server-authoritative
	 *  and happens in Die(); this only syncs the visual/input feedback. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnDeath();

public:

	/** Returns true if the character is dead */
	bool IsDead() const;

	/** Sets the team ID for this character */
	void SetTeam(uint8 Team);

	/** Returns the team ID for this character */
	uint8 GetTeamByte() const { return TeamByte; }

	//~Begin Spawn protection (TDM invincibility on respawn)
	/** Grants SpawnProtectionTime seconds of invincibility. */
	void GrantSpawnProtection(float Duration);
	/** Clears spawn protection immediately. */
	void ClearSpawnProtection();
	/** Returns true while the character is spawn-protected (invincible). */
	bool IsSpawnProtected() const { return bIsSpawnProtected; }
	//~End Spawn protection

	//~Begin Respawn loadout (TDM weapon pick)
	/** Opens the respawn loadout panel (called by the player controller). */
	UFUNCTION(BlueprintCallable, Category = "Loadout")
	void OpenLoadout();

	/** Closes the loadout panel and equips the pending weapon. */
	UFUNCTION(BlueprintCallable, Category = "Loadout")
	void CloseLoadout();

	/** Sets the weapon the player has pre-selected in the loadout panel. */
	UFUNCTION(BlueprintCallable, Category = "Loadout")
	void SetPendingLoadoutWeapon(TSubclassOf<AShooterWeapon> Weapon);

	/** Returns true while the loadout panel is open. */
	bool IsLoadoutOpen() const { return bLoadoutOpen; }
	//~End Respawn loadout
};
