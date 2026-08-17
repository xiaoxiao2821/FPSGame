// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterLoginPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "UObject/ConstructorHelpers.h"

AShooterLoginPlayerController::AShooterLoginPlayerController()
{
	// Default the login menu to BP_LoginMenu (create it at this path in the
	// editor; the PlayerController Blueprint can override LoginMenuClass).
	static ConstructorHelpers::FClassFinder<UUserWidget> MenuFinder(
		TEXT("/Game/Variant_Shooter/Blueprints/Login/BP_LoginMenu.BP_LoginMenu_C"));
	if (MenuFinder.Succeeded())
	{
		LoginMenuClass = MenuFinder.Class;
	}
}

void AShooterLoginPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// CLIENT-side entry point: only the LOCAL controller shows the menu.
	// (GameMode::BeginPlay runs on the server only, so it can never display UI
	//  on a network client — the menu must be owned by the local controller.)
	if (!IsLocalController())
	{
		return;
	}

	if (!LoginMenuClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("LOGIN: LoginMenuClass is NULL - create BP_LoginMenu at /Game/Variant_Shooter/Blueprints/Login/"));
		return;
	}

	LoginMenuWidget = CreateWidget<UUserWidget>(this, LoginMenuClass);
	if (!LoginMenuWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("LOGIN: failed to create login menu widget"));
		return;
	}

	LoginMenuWidget->AddToViewport(100);
	UE_LOG(LogTemp, Warning, TEXT("LOGIN: login menu shown (client-side)"));

	// Switch to UI-only input so the player can click the button and type into
	// the address/port boxes.
	bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
}
