// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "FPSCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A basic first person character
 */
UCLASS(abstract)
class AFPSCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	class UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	class UInputAction* MouseLookAction;

public:
	AFPSCharacter();

protected:

	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Local-only: keeps the first-person arms/weapon mesh pitched with the camera. */
	virtual void Tick(float DeltaTime) override;

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	

public:

	/** Returns the first person mesh **/
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

	/**
	 * Safely returns the aim pitch (control rotation pitch) for use in Animation Blueprints.
	 * Returns 0.0f if the controller is not valid (e.g. before possession or for unpossessed pawns).
	 * Use this in AnimBP EventGraph instead of GetController() -> GetControlRotation() to avoid
	 * runtime errors when the pawn has no controller.
	 */
	UFUNCTION(BlueprintPure, Category="Animation")
	float GetAimPitch() const;

	/**
	 * Safely returns the aim yaw (control rotation yaw) for use in Animation Blueprints.
	 * Returns 0.0f if the controller is not valid.
	 */
	UFUNCTION(BlueprintPure, Category="Animation")
	float GetAimYaw() const;

	/**
	 * Safely returns the aim forward vector (control rotation's unit forward vector) for Animation Blueprints.
	 * Replaces the fragile GetController() -> GetControlRotation() -> GetForwardVector() chain that throws
	 * "read property ... of None" when the pawn has no controller (before possession / unpossessed).
	 * Returns (1,0,0) when there is no controller, so any downstream dot product yields 0 (no aim) instead of crashing.
	 */
	UFUNCTION(BlueprintPure, Category="Animation")
	FVector GetAimForwardVector() const;

	/** Tints the third-person mesh with the team color (0 = RED, 1 = BLUE).
	 *  Uses the Mannequin material's "Paint Tint"/"LogoTint" parameters so the
	 *  original look is preserved, just recolored. No-op if parameters are absent. */
	void ApplyTeamColor(uint8 Team);

};

