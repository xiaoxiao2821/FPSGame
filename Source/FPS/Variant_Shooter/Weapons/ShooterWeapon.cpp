// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterWeapon.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "ShooterProjectile.h"
#include "ShooterWeaponHolder.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterTDMGameMode.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

AShooterWeapon::AShooterWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// Replicate the weapon so the server is the authority on firing/ammo and
	// every client sees the attached weapon mesh. The weapon's own transform is
	// NOT replicated — it follows the owner via the (replicated) actor attach.
	SetReplicates(true);
	SetReplicateMovement(false);

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the first person mesh
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);

	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	// create the third person mesh
	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);

	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();

	// subscribe to the owner's destroyed delegate
	GetOwner()->OnDestroyed.AddDynamic(this, &AShooterWeapon::OnOwnerDestroyed);

	// cast the weapon owner
	WeaponOwner = Cast<IShooterWeaponHolder>(GetOwner());
	PawnOwner = Cast<APawn>(GetOwner());

	// fill the first ammo clip
	CurrentBullets = MagazineSize;

	// Per-weapon reload time default: 0.5s pistol / 0.8s rifle / 1.0s grenade
	// launcher. Only applied when the Blueprint didn't explicitly override the
	// base 0.5s value (i.e. it still equals the C++ default).
	if (ReloadTime == 0.5f)
	{
		const FString ClassName = GetClass()->GetName();
		if (ClassName.Contains(TEXT("Grenade")))
		{
			ReloadTime = 1.0f;
		}
		else if (ClassName.Contains(TEXT("Rifle")))
		{
			ReloadTime = 0.8f;
		}
	}

	// attach the meshes to the owner
	WeaponOwner->AttachWeaponMeshes(this);
	if (FirstPersonMesh && FirstPersonMesh->GetAttachParent() != nullptr)
	{
		bMeshesAttached = true;
	}

	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("AMMO_UI: %s BeginPlay(client) owner=%s WeaponOwner=%s bullets=%d mag=%d"),
			*GetName(), *GetNameSafe(GetOwner()),
			WeaponOwner ? TEXT("VALID") : TEXT("NULL"),
			CurrentBullets, MagazineSize);

		// Client: push the initial ammo count to the HUD once everything is
		// ready. The replicated CurrentBullets equals what we just set locally
		// (=MagazineSize), so OnRep_CurrentBullets does NOT fire on spawn, and
		// OnRep_CurrentWeapon fired before the PlayerController subscribed.
		// This weapon only replicates AFTER the owner pawn was possessed, so by
		// the time this fires the owning PC is subscribed and its HUD exists —
		// the initial ammo UI is guaranteed to appear. (NPC weapons: the NPC's
		// UpdateWeaponHUD is a no-op, so this is harmless.)
		GetWorldTimerManager().SetTimer(InitialAmmoReportTimer, [this]()
		{
			UE_LOG(LogTemp, Warning, TEXT("AMMO_UI: %s initial-push fire owner=%s WeaponOwner=%s bullets=%d mag=%d"),
				*GetName(), *GetNameSafe(GetOwner()),
				WeaponOwner ? TEXT("VALID") : TEXT("NULL"),
				CurrentBullets, MagazineSize);
			if (WeaponOwner)
			{
				WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
			}
		}, 0.1f, false);
	}
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
	GetWorld()->GetTimerManager().ClearTimer(InitialAmmoReportTimer);
	GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
}

void AShooterWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Retry the local mesh attachment until it succeeds. Component-level attach
	// does NOT replicate: if this machine's owner mesh wasn't ready when
	// BeginPlay ran, the weapon mesh would float at a wrong spot — retrying for
	// a few frames fixes that.
	if (!bMeshesAttached && WeaponOwner)
	{
		WeaponOwner->AttachWeaponMeshes(this);
		if (FirstPersonMesh && FirstPersonMesh->GetAttachParent() != nullptr)
		{
			bMeshesAttached = true;
		}
	}

	// While the owning client holds the trigger, stream the latest aim point
	// to the server (throttled) so sustained fire follows the moving crosshair.
	if (!HasAuthority() && bIsFiring && PawnOwner)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now - LastAimReportTime >= 0.05f)
		{
			LastAimReportTime = Now;
			if (AShooterCharacter* OwnerChar = Cast<AShooterCharacter>(PawnOwner))
			{
				ServerUpdateAim(OwnerChar->GetWeaponTargetLocation(), GetMuzzleWorldLocation());
			}
		}
	}
}

void AShooterWeapon::ServerUpdateAim_Implementation(FVector AimTarget, FVector MuzzleWorld)
{
	if (!HasAuthority())
	{
		return;
	}

	// Refresh the aim point mid-burst (unreliable: a dropped update is fine,
	// the next one arrives within 50ms).
	if (!AimTarget.IsZero())
	{
		PendingAimTarget = AimTarget;
	}
	if (!MuzzleWorld.IsZero())
	{
		PendingMuzzleLocation = MuzzleWorld;
	}
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// ensure this weapon is destroyed when the owner is destroyed
	Destroy();
}

void AShooterWeapon::ActivateWeapon(const FName& OwnerTag)
{
	// save the owner tag for perception noise detection
	NoiseOwnerTag = OwnerTag;

	// unhide this weapon
	SetActorHiddenInGame(false);

	// notify the owner
	WeaponOwner->OnWeaponActivated(this);
}

void AShooterWeapon::DeactivateWeapon()
{
	// ensure we're no longer firing this weapon while deactivated
	StopFiring();

	// hide the weapon
	SetActorHiddenInGame(true);

	// notify the owner
	WeaponOwner->OnWeaponDeactivated(this);
}

void AShooterWeapon::StartFiring(const FVector& AimTarget, const FVector& MuzzleWorld)
{
	// Raise the local flag for input feel; the actual shots are fired on the
	// server so projectile spawn and damage are authoritative.
	bIsFiring = true;

	if (HasAuthority())
	{
		// Server (e.g. AI bot): remember the requested aim point, or let Fire
		// compute one from the server-side target later.
		if (!AimTarget.IsZero())
		{
			PendingAimTarget = AimTarget;
		}
		if (!MuzzleWorld.IsZero())
		{
			PendingMuzzleLocation = MuzzleWorld;
		}
		StartFiringInternal();
	}
	else
	{
		// Send both the aim point (with camera pitch) and the muzzle position
		// (accurate on the client's FP mesh) so bullets leave the barrel.
		ServerStartFiring(AimTarget, GetMuzzleWorldLocation());
	}
}

FVector AShooterWeapon::GetMuzzleWorldLocation() const
{
	if (FirstPersonMesh && FirstPersonMesh->DoesSocketExist(MuzzleSocketName))
	{
		return FirstPersonMesh->GetSocketLocation(MuzzleSocketName);
	}

	// MuzzleSocketName is empty by default, so fall back to the owner's EYE
	// position — not the actor root. Spawning from the root (feet/waist height)
	// tilted the shot low and made gravity-affected rounds (grenade launcher)
	// land below the aim point ("initial aim looks wrong").
	if (PawnOwner)
	{
		FVector EyeLoc;
		FRotator EyeRot;
		PawnOwner->GetActorEyesViewPoint(EyeLoc, EyeRot);
		return EyeLoc;
	}
	return GetActorLocation();
}

void AShooterWeapon::StopFiring()
{
	// Lower the firing flag and stop the refire timer (authoritative on server).
	bIsFiring = false;

	if (HasAuthority())
	{
		GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
	}
	else
	{
		ServerStopFiring();
	}
}

void AShooterWeapon::ServerStartFiring_Implementation(FVector AimTarget, FVector MuzzleWorld)
{
	if (!HasAuthority())
	{
		return;
	}

	// Reject fire requests from a dead owner (the server owns the real HP).
	if (const AShooterCharacter* OwnerChar = Cast<AShooterCharacter>(PawnOwner))
	{
		if (OwnerChar->IsDead())
		{
			bIsFiring = false;
			return;
		}
	}

	// Remember the client's aim point (camera pitch) and muzzle position so the
	// server spawns the projectile exactly where the client's gun barrel is.
	if (!AimTarget.IsZero())
	{
		PendingAimTarget = AimTarget;
	}
	if (!MuzzleWorld.IsZero())
	{
		PendingMuzzleLocation = MuzzleWorld;
	}

	bIsFiring = true;
	StartFiringInternal();
}

void AShooterWeapon::ServerStopFiring_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}

	bIsFiring = false;
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::StartFiringInternal()
{
	// check how much time has passed since we last shot
	// this may be under the refire rate if the weapon shoots slow enough and the player is spamming the trigger
	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;

	if (TimeSinceLastShot > RefireRate)
	{
		// fire the weapon right away
		Fire();

	} else {

		// still on cooldown: schedule the pending shot.
		// For full-auto this chains into the refire loop; for semi-auto this
		// makes a quick re-press (sooner than the cooldown) fire one more shot
		// instead of being swallowed (fixes "hold fire stops working").
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, TimeSinceLastShot, false);

	}
}

void AShooterWeapon::Fire()
{
	// Server-authoritative: only the server spawns projectiles and applies damage.
	if (!HasAuthority())
	{
		return;
	}

	// Cannot fire while the magazine is being refilled.
	if (bIsReloading)
	{
		return;
	}

	// ensure the player still wants to fire. They may have let go of the trigger
	if (!bIsFiring)
	{
		return;
	}
	
	// Resolve the aim point: prefer the client-sent point (full pitch), fall
	// back to the server-side computed one (AI bots with no explicit target).
	const FVector AimTarget = !PendingAimTarget.IsZero()
		? PendingAimTarget
		: WeaponOwner->GetWeaponTargetLocation();

	// Resolve the start: the client-observed muzzle (bullets leave the barrel),
	// or the eye position for AI bots.
	FVector StartLocation = PendingMuzzleLocation;
	if (!StartLocation.IsZero() && PawnOwner)
	{
		// Sanity check: if the reported muzzle is absurdly far from the owner
		// (desynced FP mesh/socket), fall back to the eye so shots stay on the
		// character instead of spawning halfway across the map.
		if (FVector::Dist(StartLocation, PawnOwner->GetActorLocation()) > 500.0f)
		{
			StartLocation = FVector::ZeroVector;
		}
	}
	if (StartLocation.IsZero() && PawnOwner)
	{
		FVector EyeLoc;
		FRotator EyeRot;
		PawnOwner->GetActorEyesViewPoint(EyeLoc, EyeRot);
		StartLocation = EyeLoc - FVector(0.0f, 0.0f, 6.0f);
	}
	if (StartLocation.IsZero())
	{
		StartLocation = GetActorLocation();
	}

	// fire a projectile at the target (spawned on the server, simulated by the server)
	FireProjectile(AimTarget, StartLocation);

	// update the time of our last shot
	TimeOfLastShot = GetWorld()->GetTimeSeconds();

	// make noise so the AI perception system can hear us
	MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, NoiseOwnerTag);

	// broadcast the firing feedback (montage / recoil / ammo HUD) to every client
	Multicast_OnFire();

	// are we full auto?
	if (bFullAuto)
	{
		// schedule the next shot
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, RefireRate, false);
	} else {

		// for semi-auto weapons, schedule the cooldown notification
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::FireCooldownExpired, RefireRate, false);

	}
}

void AShooterWeapon::Multicast_OnFire_Implementation()
{
	// Presentation only — all gameplay state was already updated on the server.
	WeaponOwner->PlayFiringMontage(FiringMontage);
	WeaponOwner->AddWeaponRecoil(FiringRecoil);
	WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
}

void AShooterWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Ammo count: the server is authoritative; every client receives the
	// change and fires the local "bullets changed" event via OnRep.
	DOREPLIFETIME(AShooterWeapon, CurrentBullets);

	// Reload state: replicated so clients can show a "reloading" indicator.
	DOREPLIFETIME(AShooterWeapon, bIsReloading);
}

void AShooterWeapon::OnRep_IsReloading()
{
	// The replicated reload state changed — notify the owner character so its
	// HUD can show/hide the "reloading" indicator (reception is guaranteed to
	// be the locally controlled PC via its OnPossess/SetPawn subscription).
	NotifyReloadState();
}

void AShooterWeapon::NotifyReloadState()
{
	if (AShooterCharacter* OwnerChar = Cast<AShooterCharacter>(PawnOwner))
	{
		OwnerChar->OnReloadStateChanged.Broadcast(bIsReloading);
	}
}

void AShooterWeapon::OnRep_CurrentBullets()
{
	UE_LOG(LogTemp, Warning, TEXT("AMMO_UI: %s OnRep_CurrentBullets bullets=%d mag=%d WeaponOwner=%s"),
		*GetName(), CurrentBullets, MagazineSize,
		WeaponOwner ? TEXT("VALID") : TEXT("NULL"));

	// The server's authoritative ammo count arrived — fire the local
	// "bullets changed" event so the owning client's HUD refreshes. This
	// runs on every client; the weapon holder only broadcasts to a local HUD.
	if (WeaponOwner)
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation, const FVector& StartLocation)
{
	// Spawn a fresh projectile every shot. Pooled reuse caused replicated
	// visibility/position desync (bullets flying in from their old spot,
	// invisible bullets, ghost bullets elsewhere) because the client's pooled
	// instance can never reliably re-sync position/visibility. A fresh actor
	// starts at the correct muzzle transform, so clients always see it right.
	FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation, StartLocation);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = PawnOwner;

	AShooterProjectile* Projectile = GetWorld()->SpawnActor<AShooterProjectile>(ProjectileClass, ProjectileTransform, SpawnParams);
	if (Projectile)
	{
		Projectile->SetNoiseTag(NoiseOwnerTag);
	}

	// consume bullets
	--CurrentBullets;

	// magazine empty: start the reload (server-authoritative delayed refill;
	// CurrentBullets replicates to every client and refreshes the HUD)
	if (CurrentBullets <= 0)
	{
		StartReload();
	}
}

void AShooterWeapon::StartReload()
{
	if (bIsReloading || !HasAuthority())
	{
		return;
	}

	bIsReloading = true;

	// Notify locally (server / ListenServer) — remote clients get it via the
	// replicated property (OnRep_IsReloading).
	NotifyReloadState();

	// Refill the magazine after the weapon's reload time. During this window
	// Fire() is blocked and the HUD shows an empty magazine (CurrentBullets=0
	// is already replicated from the last shot).
	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &AShooterWeapon::FinishReload, ReloadTime, false);
}

void AShooterWeapon::FinishReload()
{
	bIsReloading = false;

	// Notify locally; clients get it via OnRep_IsReloading.
	NotifyReloadState();

	// Refill the clip (replicated -> owning client's HUD updates to full).
	CurrentBullets = MagazineSize;
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(const FVector& TargetLocation, const FVector& StartLocation) const
{
	// Spawn exactly where the client's gun muzzle is (StartLocation), aiming at
	// the client's crosshair target — bullets leave the barrel and hit what the
	// player aims at. Zero start falls back to the actor location.
	const FVector SpawnLoc = !StartLocation.IsZero() ? StartLocation : GetActorLocation();

	// aim the projectile at the target (with configured variance)
	const FRotator AimRot = UKismetMathLibrary::FindLookAtRotation(
		SpawnLoc, TargetLocation + (UKismetMathLibrary::RandomUnitVector() * AimVariance));

	// return the built transform
	return FTransform(AimRot, SpawnLoc, FVector::OneVector);
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}
