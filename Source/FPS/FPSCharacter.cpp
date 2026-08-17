// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "FPS.h"

AFPSCharacter::AFPSCharacter()
{
	// Replicate this character to every client (dedicated-server friendly):
	// movement is driven by the server, damage/death are server-authoritative.
	SetReplicates(true);
	SetReplicateMovement(true);

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	
	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create the Camera Component
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("HeadSlot"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	// Keep third-person animation updating on every machine even when the pawn
	// is off-screen or idle — otherwise replicated characters can look wrong
	// (frozen/stepped poses) when they come back into view.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;

	// Tick is only used to aim the first-person mesh with the camera (local-only).
	PrimaryActorTick.bCanEverTick = true;
}

void AFPSCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// First-person arms + weapon follow the camera's pitch. The camera itself is
	// driven by bUsePawnControlRotation (so it stays exact), while the FP mesh
	// only takes the pitch component — this makes the gun point where you look.
	// Local-only: remote pawns render through their replicated third-person view.
	if (IsLocallyControlled() && FirstPersonMesh)
	{
		FirstPersonMesh->SetRelativeRotation(FRotator(GetControlRotation().Pitch, 0.0f, 0.0f));
	}
}

void AFPSCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AFPSCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AFPSCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFPSCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFPSCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AFPSCharacter::LookInput);
	}
	else
	{
		UE_LOG(LogFPS, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}


void AFPSCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);

}

void AFPSCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);

}

void AFPSCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AFPSCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AFPSCharacter::DoJumpStart()
{
	// pass Jump to the character
	Jump();
}

void AFPSCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}

float AFPSCharacter::GetAimPitch() const
{
	// safely return the control rotation pitch, or 0 if no controller
	if (const AController* Ctrl = GetController())
	{
		return Ctrl->GetControlRotation().Pitch;
	}
	return 0.0f;
}

float AFPSCharacter::GetAimYaw() const
{
	// safely return the control rotation yaw, or 0 if no controller
	if (const AController* Ctrl = GetController())
	{
		return Ctrl->GetControlRotation().Yaw;
	}
	return 0.0f;
}

FVector AFPSCharacter::GetAimForwardVector() const
{
	// Null-safe replacement for the AnimBP chain:
	//   GetController() -> GetControlRotation() -> GetForwardVector()
	// that crashes with "read property ... of None" before possession.
	if (const AController* Ctrl = GetController())
	{
		return Ctrl->GetControlRotation().Vector(); // unit forward vector (X axis)
	}
	return FVector::ForwardVector; // (1,0,0): dot with world Up = 0 => no aim, safe
}

void AFPSCharacter::ApplyTeamColor(uint8 Team)
{
	if (!GetMesh())
	{
		return;
	}

	// 0 = RED, 1 = BLUE
	const FLinearColor TeamColor = (Team == 0)
		? FLinearColor(0.85f, 0.15f, 0.15f)
		: FLinearColor(0.15f, 0.25f, 0.9f);

	// Tint every material slot of the third-person mesh via dynamic instances
	// (keeps the original material, only recolors the Mannequin parameters).
	for (int32 i = 0; i < GetMesh()->GetNumMaterials(); ++i)
	{
		if (UMaterialInstanceDynamic* MID = GetMesh()->CreateAndSetMaterialInstanceDynamic(i))
		{
			MID->SetVectorParameterValue(FName("Paint Tint"), TeamColor);
			MID->SetVectorParameterValue(FName("LogoTint"), TeamColor);
		}
	}
}
