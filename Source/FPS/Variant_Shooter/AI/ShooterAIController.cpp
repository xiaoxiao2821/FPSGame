// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterAIController.h"
#include "ShooterNPC.h"
#include "ShooterCharacter.h"
#include "Components/StateTreeAIComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/PathFollowingAgentInterface.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

AShooterAIController::AShooterAIController()
{
	// Enable tick so the C++ fallback AI can run (the StateTree asset is
	// corrupted and cannot drive the bot — see BeginPlay).
	PrimaryActorTick.bCanEverTick = true;

	// create the StateTree component
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
	StateTreeAI->SetStartLogicAutomatically(false);

	// create the AI perception component. It will be configured in BP
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

	// subscribe to the AI perception delegates
	AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &AShooterAIController::OnPerceptionUpdated);
	AIPerception->OnTargetPerceptionForgotten.AddDynamic(this, &AShooterAIController::OnPerceptionForgotten);
}

void AShooterAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// ensure we're possessing an NPC
	if (AShooterNPC* NPC = Cast<AShooterNPC>(InPawn))
	{
		// add the team tag to the pawn
		NPC->Tags.Add(TeamTag);

		// subscribe to the pawn's OnDeath delegate
		NPC->OnPawnDeath.AddDynamic(this, &AShooterAIController::OnPawnDeath);

	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: AIController %s possessed %s (team %u)"),
		*GetName(), *NPC->GetName(), NPC->GetTeamByte());
	}
}

void AShooterAIController::BeginPlay()
{
	Super::BeginPlay();

	// The ST_Shooter asset's runtime data is corrupted: StartLogic returns
	// Succeeded immediately with zero active states, so the tree never runs.
	// Instead of the tree, the C++ fallback AI (Tick) drives the bot.
	LastAimScanTime = -1.0f;
	UE_LOG(LogTemp, Warning, TEXT("TDM_FLOW: AIController %s BeginPlay - C++ fallback AI active (StateTree asset corrupted)"), *GetName());
}

void AShooterAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Server-authoritative: only the server runs AI logic.
	if (!HasAuthority())
	{
		return;
	}

	AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	if (!NPC || NPC->IsDead())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	// Periodic enemy scan (throttled).
	if (Now - LastAimScanTime >= AimScanInterval)
	{
		LastAimScanTime = Now;
		FindEnemy();
	}

	// Combat or roam decision.
	if (TargetEnemy)
	{
		UpdateCombatBehavior(DeltaTime);
	}
	else
	{
		// No enemy: stop shooting and roam.
		NPC->StopShooting();

		if (Now >= RoamCooldownUntil)
		{
			const EPathFollowingStatus::Type MoveStatus = GetMoveStatus();
			if (MoveStatus != EPathFollowingStatus::Moving || !bIsRoaming)
			{
				RoamToRandomPoint();
			}
		}
	}
}

bool AShooterAIController::FindEnemy()
{
	AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	if (!NPC)
	{
		TargetEnemy = nullptr;
		return false;
	}

	const uint8 MyTeam = NPC->GetTeamByte();
	const FVector MyLoc = NPC->GetActorLocation();
	const float BestDistSqMax = SightRadius * SightRadius;

	AActor* Best = nullptr;
	float BestDistSq = BestDistSqMax;

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShooterCharacter::StaticClass(), Found);
	for (AActor* Actor : Found)
	{
		if (Actor == NPC)
		{
			continue;
		}
		AShooterCharacter* Char = Cast<AShooterCharacter>(Actor);
		if (!Char || Char->IsDead())
		{
			continue;
		}
		const uint8 TheirTeam = Char->GetTeamByte();
		if (TheirTeam == 255 || TheirTeam == MyTeam)
		{
			continue; // same team or unassigned: ignore
		}
		const float DistSq = FVector::DistSquared(MyLoc, Char->GetActorLocation());
		if (DistSq > BestDistSq)
		{
			continue;
		}
		if (!HasLineOfSightTo(Char))
		{
			continue;
		}
		BestDistSq = DistSq;
		Best = Char;
	}

	TargetEnemy = Best;
	return Best != nullptr;
}

void AShooterAIController::UpdateCombatBehavior(float DeltaTime)
{
	AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	if (!NPC)
	{
		return;
	}

	AShooterCharacter* Enemy = Cast<AShooterCharacter>(TargetEnemy);
	if (!Enemy || Enemy->IsDead() || Enemy->GetTeamByte() == NPC->GetTeamByte() || Enemy->GetTeamByte() == 255)
	{
		// Target lost / invalid: drop it and stop firing.
		TargetEnemy = nullptr;
		NPC->StopShooting();
		return;
	}

	const FVector MyLoc = NPC->GetActorLocation();
	const FVector EnemyLoc = Enemy->GetActorLocation();
	const float Dist = FVector::Dist2D(MyLoc, EnemyLoc);

	// Face the enemy (yaw + pitch so the weapon aims at them).
	const FRotator LookRot = (EnemyLoc - MyLoc).Rotation();
	SetControlRotation(FRotator(LookRot.Pitch, LookRot.Yaw, 0.0f));

	if (Dist > EngageMaxDistance)
	{
		// Too far: approach, hold fire.
		NPC->StopShooting();
		MoveToActor(Enemy, EngageMinDistance * 0.8f);
		bIsRoaming = false;
	}
	else
	{
		// In engagement range: stop and fire continuously.
		StopMovement();
		NPC->StartShooting(Enemy);
		bIsRoaming = false;
	}
}

void AShooterAIController::RoamToRandomPoint()
{
	AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	if (!NPC)
	{
		return;
	}

	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetNavigationSystem(GetWorld()))
	{
		FNavLocation Point;
		if (NavSys->GetRandomPointInNavigableRadius(NPC->GetActorLocation(), RoamRadius, Point))
		{
			RoamTargetLocation = Point.Location;
			MoveToLocation(Point.Location, 50.0f);
			bIsRoaming = true;
			UE_LOG(LogTemp, Verbose, TEXT("TDM_FLOW: %s roam -> %s"), *GetName(), *Point.Location.ToString());
		}
		else
		{
			bIsRoaming = false;
		}
	}

	// Pause before the next roam move, whether this one succeeded or not.
	RoamCooldownUntil = GetWorld()->GetTimeSeconds() + RoamPauseTime;
}

bool AShooterAIController::HasLineOfSightTo(const AActor* Target) const
{
	const APawn* P = GetPawn();
	if (!P || !Target)
	{
		return false;
	}

	const FVector Start = P->GetActorLocation() + FVector(0.0f, 0.0f, 85.0f);
	const FVector End = Target->GetActorLocation() + FVector(0.0f, 0.0f, 85.0f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShooterAILineOfSight), true);
	Params.AddIgnoredActor(P);
	Params.AddIgnoredActor(Target);

	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
}

void AShooterAIController::OnPawnDeath()
{
	// stop movement
	GetPathFollowingComponent()->AbortMove(*this, FPathFollowingResultFlags::UserAbort);

	// stop any firing
	if (AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn()))
	{
		NPC->StopShooting();
	}

	// stop StateTree logic (harmless if never started)
	StateTreeAI->StopLogic(FString(""));

	// unpossess the pawn
	UnPossess();

	// destroy this controller
	Destroy();
}

void AShooterAIController::SetCurrentTarget(AActor* Target)
{
	TargetEnemy = Target;
}

void AShooterAIController::ClearCurrentTarget()
{
	TargetEnemy = nullptr;
}

void AShooterAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	// pass the data to the StateTree delegate hook (unused by fallback AI)
	OnShooterPerceptionUpdated.ExecuteIfBound(Actor, Stimulus);
}

void AShooterAIController::OnPerceptionForgotten(AActor* Actor)
{
	// pass the data to the StateTree delegate hook (unused by fallback AI)
	OnShooterPerceptionForgotten.ExecuteIfBound(Actor);
}
