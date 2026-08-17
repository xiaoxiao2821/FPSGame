// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterProjectile.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/OverlapResult.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Variant_Shooter/ShooterTDMGameMode.h"

AShooterProjectile::AShooterProjectile()
{
	PrimaryActorTick.bCanEverTick = true;

	// Replicate the projectile so clients see the server-simulated flight;
	// NotifyHit then only fires on the server -> damage is authoritative.
	SetReplicates(true);
	SetReplicateMovement(true);

	// Fast-moving projectile: a moderate replication rate keeps the flight
	// smooth without saturating the network (200Hz caused visible hitches with
	// many pooled bullets updating at once; 150Hz tightens client position
	// agreement while staying cheap).
	SetNetUpdateFrequency(150.0f);

	// create the collision component and assign it as the root
	RootComponent = CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Collision Component"));

	CollisionComponent->SetSphereRadius(16.0f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;

	// create the projectile movement component. No need to attach it because it's not a Scene Component
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Projectile Movement"));

	ProjectileMovement->InitialSpeed = 3000.0f;
	ProjectileMovement->MaxSpeed = 3000.0f;
	ProjectileMovement->bShouldBounce = true;

	// Anti-tunneling: sub-step the simulation so a 3000 cm/s bullet never
	// skips over a target between ticks. The engine default is 50ms per step
	// (=150cm — wider than a pawn's 34cm-radius capsule), which is why bullets
	// "passed straight through". 8ms/step keeps each physics sweep ~25cm so
	// the sphere always overlaps the capsule. MaxSimulationIterations covers a
	// low-rate server frame (30Hz = 33ms needs 4 steps; 8 leaves headroom).
	ProjectileMovement->MaxSimulationTimeStep = 1.0f / 120.0f;
	ProjectileMovement->MaxSimulationIterations = 8;

	// NOTE: gravity is intentionally left at the Blueprint's configured value —
	// the grenade launcher's parabolic arc is by design, and forcing 0 here
	// would override it. See TDM_需求文档.md §6.

	// set the default damage type
	HitDamageType = UDamageType::StaticClass();
}

void AShooterProjectile::BeginPlay()
{
	Super::BeginPlay();

	// Force the collision state (in case a Blueprint override changed it) so the
	// server sweep ALWAYS detects pawns — this is what makes player-vs-player
	// damage work. All clients see the same server-simulated flight.
	if (HasAuthority())
	{
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		CollisionComponent->SetCollisionObjectType(ECC_WorldDynamic);
		CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
		CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);

		// Origin of this shot's path sweep (see Tick).
		LastTraceLocation = GetActorLocation();

		// No-hit safety timeout: a projectile that never hits anything is
		// destroyed shortly after leaving the map (no permanent leftovers).
		GetWorld()->GetTimerManager().SetTimer(
			DestructionTimer, this, &AShooterProjectile::ReturnToPool, 3.0f, false);
	}
	else
	{
		// Clients SIMULATE the flight locally so the visual is smooth (no
		// stepping from replication), but they do NOT do physics collision:
		// with QueryOnly, the client never fires its own NotifyHit — hit/stop/
		// hide is driven entirely by the server (authoritative), so bullets
		// don't disappear early or diverge.
		ProjectileMovement->SetComponentTickEnabled(true);
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}

	// ignore the pawn that shot this projectile
	CollisionComponent->IgnoreActorWhenMoving(GetInstigator(), true);
}

void AShooterProjectile::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the destruction timer
	GetWorld()->GetTimerManager().ClearTimer(DestructionTimer);
}

void AShooterProjectile::NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	// Shared impact handling — also used by the server's per-frame path sweep
	// (Tick) so a hit always produces exactly the same damage/effects whether
	// the physics sub-step caught it or the sweep did.
	HandleImpact(Hit, Other, OtherComp, Hit.ImpactPoint, Hit.ImpactNormal);
}

void AShooterProjectile::HandleImpact(const FHitResult& Hit, AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation, const FVector& HitNormal)
{
	// ignore if we've already hit something else
	if (bHit)
	{
		return;
	}

	bHit = true;

	// Snap to the impact point so explosions/damage originate exactly at the
	// surface — the server path sweep can catch a target mid-flight, slightly
	// beyond the contact point.
	if (HasAuthority())
	{
		SetActorLocation(HitLocation);
	}

	// disable collision on the projectile
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// make AI perception noise
	MakeNoise(NoiseLoudness, GetInstigator(), GetActorLocation(), NoiseRange, NoiseTag);

	if (HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("PROJ_HIT: %s hit %s (instigator=%s)"),
			*GetName(), *GetNameSafe(HitActor), *GetNameSafe(GetInstigator()));
	}

	if (bExplodeOnHit)
	{

		// apply explosion damage centered on the impact point
		ExplosionCheck(HitLocation);

	} else {

		// single hit projectile. Process the collided actor
		ProcessHit(HitActor, HitComp, HitLocation, -HitNormal);

	}

	// pass control to BP for any extra effects (broadcast to every client;
	// the damage/explosion logic above already ran authoritatively on the server)
	Multicast_OnHit(Hit);

	// Requirement: on hit the projectile stops, becomes invisible immediately
	// (no lingering/flashback), and is destroyed 1 second later.
	if (HasAuthority())
	{
		ProjectileMovement->StopMovementImmediately();
		SetActorHiddenInGame(true);
		GetWorld()->GetTimerManager().SetTimer(
			DestructionTimer, this, &AShooterProjectile::ReturnToPool, 1.0f, false);
	}
}

void AShooterProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Server-side path sweep: guarantees a fast bullet cannot tunnel through a
	// target between physics sub-steps (e.g. when the server runs at a low
	// frame rate). Clients never run this — they only simulate locally for
	// looks and take hit/stop/hide from the server.
	if (!HasAuthority() || bHit || bPooled)
	{
		return;
	}

	// Sweep the segment moved THIS frame (previous recorded position -> now).
	// If the movement component already ticked (component ticks before the
	// actor), CurrentLoc is the end-of-frame position and the segment covers
	// the full frame; otherwise predict from velocity so the sweep still
	// covers the frame's travel.
	FVector CurrentLoc = GetActorLocation();
	const FVector PrevLoc = LastTraceLocation;

	if (FVector::DistSquared(CurrentLoc, PrevLoc) <= 1.0f)
	{
		// No measured movement (movement component may not have ticked yet) —
		// predict the travel from velocity.
		if (ProjectileMovement && ProjectileMovement->Velocity.SizeSquared() > 0.0f)
		{
			CurrentLoc = CurrentLoc + ProjectileMovement->Velocity * DeltaTime;
		}
		else
		{
			return;
		}
	}
	LastTraceLocation = CurrentLoc;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(GetInstigator());
	Params.AddIgnoredActor(GetOwner());
	Params.bTraceComplex = false;

	const float Radius = CollisionComponent ? CollisionComponent->GetScaledSphereRadius() : 16.0f;

	FHitResult Hit;
	if (GetWorld()->SweepSingleByChannel(
			Hit, PrevLoc, CurrentLoc, FQuat::Identity,
			ECC_WorldDynamic,
			FCollisionShape::MakeSphere(Radius),
			Params))
	{
		HandleImpact(Hit, Hit.GetActor(), Hit.GetComponent(), Hit.ImpactPoint, Hit.ImpactNormal);
	}
}

void AShooterProjectile::ExplosionCheck(const FVector& ExplosionCenter)
{
	// do a sphere overlap check look for nearby actors to damage
	TArray<FOverlapResult> Overlaps;

	FCollisionShape OverlapShape;
	OverlapShape.SetSphere(ExplosionRadius);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (!bDamageOwner)
	{
		QueryParams.AddIgnoredActor(GetInstigator());
	}

	GetWorld()->OverlapMultiByObjectType(Overlaps, ExplosionCenter, FQuat::Identity, ObjectParams, OverlapShape, QueryParams);

	TArray<AActor*> DamagedActors;

	// process the overlap results
	for (const FOverlapResult& CurrentOverlap : Overlaps)
	{
		// overlaps may return the same actor multiple times per each component overlapped
		// ensure we only damage each actor once by adding it to a damaged list
		if (DamagedActors.Find(CurrentOverlap.GetActor()) == INDEX_NONE)
		{
			DamagedActors.Add(CurrentOverlap.GetActor());

			// apply physics force away from the explosion
			const FVector& ExplosionDir = CurrentOverlap.GetActor()->GetActorLocation() - GetActorLocation();

			// push and/or damage the overlapped actor
			ProcessHit(CurrentOverlap.GetActor(), CurrentOverlap.GetComponent(), GetActorLocation(), ExplosionDir.GetSafeNormal());
		}
			
	}
}

void AShooterProjectile::ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation, const FVector& HitDirection)
{
	// have we hit a character?
	if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		// ignore the owner of this projectile
		if (HitCharacter != GetOwner() || bDamageOwner)
		{
			// apply damage to the character (server-only, authoritative)
			AController* EventInstigator = GetInstigator() ? GetInstigator()->GetController() : nullptr;
			if (HasAuthority())
			{
				UE_LOG(LogTemp, Warning, TEXT("PROJ_DMG: %s -> %s (dmg %.1f, instigator=%s)"),
					*GetName(), *HitCharacter->GetName(), HitDamage,
					EventInstigator ? *EventInstigator->GetName() : TEXT("NONE"));
			}
			UGameplayStatics::ApplyDamage(HitCharacter, HitDamage, EventInstigator, this, HitDamageType);
		}
	}

	// have we hit a physics object?
	if (HitComp->IsSimulatingPhysics())
	{
		// give some physics impulse to the object
		HitComp->AddImpulseAtLocation(HitDirection * PhysicsForce, HitLocation);
	}
}

void AShooterProjectile::Multicast_OnHit_Implementation(const FHitResult& Hit)
{
	BP_OnProjectileHit(Hit);
}

void AShooterProjectile::OnDeferredDestruction()
{
	// Legacy destroy path kept for safety — the pool flow uses ReturnToPool().
	Destroy();
}

void AShooterProjectile::ReturnToPool()
{
	// Pooling is disabled (replicated reuse desynced visibility/position on
	// clients), so "returning" simply destroys the projectile. Called 1s after
	// impact (already hidden) or after the 3s no-hit timeout.
	Destroy();
}

void AShooterProjectile::DeactivateToPool()
{
	bPooled = true;
	bHit = false;

	GetWorld()->GetTimerManager().ClearTimer(DestructionTimer);
	GetWorld()->GetTimerManager().ClearTimer(RevealTimer);
	bPendingReveal = false;
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->SetComponentTickEnabled(false);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);

	// Release the old instigator so the pooled projectile starts clean next use.
	if (LastInstigator.IsValid())
	{
		CollisionComponent->IgnoreActorWhenMoving(LastInstigator.Get(), false);
	}
	LastInstigator.Reset();
}

AShooterProjectile* AShooterProjectile::ActivateFromPool(const FTransform& Transform, AActor* NewOwner, APawn* NewInstigator, const FName& InNoiseTag)
{
	// Fully re-initialize so a reused projectile carries NO state from its last
	// shot (direction, ignored actors, rotation, movement internals).
	bPooled = false;
	bHit = false;

	// Stay hidden while the new location is being replicated: reveal shortly
	// after (RevealFromPool) so clients never render this bullet at its OLD
	// spot before the position update arrives.
	SetActorHiddenInGame(true);

	SetActorTransform(Transform);
	SetOwner(NewOwner);
	SetInstigator(NewInstigator);
	SetNoiseTag(InNoiseTag);

	SetActorEnableCollision(true);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->IgnoreActorWhenMoving(NewInstigator, true);
	LastInstigator = NewInstigator;

	// Reset the movement component's internal state, then drive it with the
	// NEW direction — otherwise the projectile keeps its previous flight.
	ProjectileMovement->SetUpdatedComponent(CollisionComponent);
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Velocity = Transform.GetRotation().Vector() * ProjectileMovement->InitialSpeed;
	ProjectileMovement->UpdateComponentVelocity();
	ProjectileMovement->SetComponentTickEnabled(true);

	// Reveal after a short delay so the new location reaches clients first.
	GetWorld()->GetTimerManager().ClearTimer(RevealTimer);
	bPendingReveal = true;
	GetWorld()->GetTimerManager().SetTimer(RevealTimer, this, &AShooterProjectile::RevealFromPool, 0.03f, false);

	// Safety: if this shot never hits anything, return it to the pool after a
	// few seconds so pooled bullets do not linger forever past the map.
	GetWorld()->GetTimerManager().ClearTimer(DestructionTimer);
	GetWorld()->GetTimerManager().SetTimer(
		DestructionTimer, this, &AShooterProjectile::ReturnToPool, 3.0f, false);

	return this;
}

void AShooterProjectile::RevealFromPool()
{
	bPendingReveal = false;
	GetWorld()->GetTimerManager().ClearTimer(RevealTimer);
	SetActorHiddenInGame(false);
}

void AShooterProjectile::SetNoiseTag(const FName& Tag)
{
	NoiseTag = Tag;
}
