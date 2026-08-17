// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ShooterAIController.generated.h"

class UStateTreeAIComponent;
class UAIPerceptionComponent;
struct FAIStimulus;

DECLARE_DELEGATE_TwoParams(FShooterPerceptionUpdatedDelegate, AActor*, const FAIStimulus&);
DECLARE_DELEGATE_OneParam(FShooterPerceptionForgottenDelegate, AActor*);

/**
 *  Simple AI Controller for a first person shooter enemy
 */
UCLASS(abstract)
class FPS_API AShooterAIController : public AAIController
{
	GENERATED_BODY()
	
	/** Runs the behavior StateTree for this NPC */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UStateTreeAIComponent* StateTreeAI;

	/** Detects other actors through sight, hearing and other senses */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UAIPerceptionComponent* AIPerception;

protected:

	/** Team tag for pawn friend or foe identification */
	UPROPERTY(EditAnywhere, Category="Shooter")
	FName TeamTag = FName("Enemy");

	/** Enemy currently being targeted */
	TObjectPtr<AActor> TargetEnemy;

	//~ C++ fallback AI (no StateTree dependency — the ST_Shooter asset's
	//~ runtime data is corrupted, see TDM_需求文档/代码梳理. This drives the
	//~ bot directly: roam -> acquire enemy -> engage -> chase -> back to roam.)

	/** Sight radius for enemy acquisition (cm). */
	UPROPERTY(EditAnywhere, Category="Shooter|AI")
	float SightRadius = 4000.0f;

	/** Below this distance the bot stops approaching and shoots. */
	UPROPERTY(EditAnywhere, Category="Shooter|AI")
	float EngageMinDistance = 350.0f;

	/** Beyond this distance the bot keeps moving toward the enemy. */
	UPROPERTY(EditAnywhere, Category="Shooter|AI")
	float EngageMaxDistance = 1200.0f;

	/** Radius used to pick random roam points. */
	UPROPERTY(EditAnywhere, Category="Shooter|AI")
	float RoamRadius = 1500.0f;

	/** How often the bot re-scans for enemies (s). */
	UPROPERTY(EditAnywhere, Category="Shooter|AI")
	float AimScanInterval = 0.4f;

	/** Pause between roam moves (s). */
	UPROPERTY(EditAnywhere, Category="Shooter|AI")
	float RoamPauseTime = 2.5f;

	/** Game time of the last enemy scan. */
	float LastAimScanTime = -1.0f;

	/** Game time after which a new roam move may be issued. */
	float RoamCooldownUntil = 0.0f;

	/** True while the bot is moving toward a roam point. */
	bool bIsRoaming = false;

	/** Current roam destination. */
	FVector RoamTargetLocation = FVector::ZeroVector;

public:

	/** Called when an AI perception has been updated. StateTree task delegate hook */
	FShooterPerceptionUpdatedDelegate OnShooterPerceptionUpdated;

	/** Called when an AI perception has been forgotten. StateTree task delegate hook */
	FShooterPerceptionForgottenDelegate OnShooterPerceptionForgotten;

public:

	/** Constructor */
	AShooterAIController();

protected:

	/** Pawn initialization */
	virtual void OnPossess(APawn* InPawn) override;

	/** AIController initialization — the StateTree asset is corrupted (its
	 *  runtime data is empty: StartLogic -> Succeeded immediately), so the
	 *  tree is NOT started. The fallback AI below runs instead. */
	virtual void BeginPlay() override;

	/** Per-frame fallback AI (server only). */
	virtual void Tick(float DeltaTime) override;

	/** Scans for the nearest enemy on the opposite team with line of sight. */
	bool FindEnemy();

	/** Drives move/shoot decisions for the current target. */
	void UpdateCombatBehavior(float DeltaTime);

	/** Picks a random reachable point and moves there. */
	void RoamToRandomPoint();

	/** True if a straight visibility trace reaches the target. */
	bool HasLineOfSightTo(const AActor* Target) const;

protected:

	/** Called when the possessed pawn dies */
	UFUNCTION()
	void OnPawnDeath();

public:

	/** Sets the targeted enemy */
	void SetCurrentTarget(AActor* Target);

	/** Clears the targeted enemy */
	void ClearCurrentTarget();

	/** Returns the targeted enemy */
	AActor* GetCurrentTarget() const { return TargetEnemy; };

protected:

	/** Called when the AI perception component updates a perception on a given actor */
	UFUNCTION()
	void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Called when the AI perception component forgets a given actor */
	UFUNCTION()
	void OnPerceptionForgotten(AActor* Actor);
};
