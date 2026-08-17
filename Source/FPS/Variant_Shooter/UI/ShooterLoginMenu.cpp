// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterLoginMenu.h"
#include "Kismet/GameplayStatics.h"

void UShooterLoginMenu::StartGame()
{
	// Build the connection target from the inputs: "IP:Port".
	// Requirement: default to the local machine (127.0.0.1) when nothing was
	// typed; the Blueprint's input boxes can override ServerAddress/ServerPort.
	FString Address = ServerAddress.TrimStartAndEnd();
	if (Address.IsEmpty())
	{
		Address = TEXT("127.0.0.1");
	}
	const FString Target = FString::Printf(TEXT("%s:%d"), *Address, ServerPort);

	UE_LOG(LogTemp, Warning, TEXT("LOGIN: connecting to DS at %s"), *Target);
	BP_OnLoginStarted();

	// Client-side travel to the DS (listen for the game level on the server).
	UGameplayStatics::OpenLevel(GetWorld(), FName(*Target), true);
}
