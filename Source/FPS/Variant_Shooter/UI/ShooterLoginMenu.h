// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterLoginMenu.generated.h"

/**
 *  Login scene start menu (shown in Lvl_Login before connecting to the DS).
 *
 *  Pure designer-owned widget: the layout (title, "start game" button, server
 *  address/port input boxes) is authored in the Blueprint child. The Blueprint
 *  binds its address/port input boxes to ServerAddress / ServerPort (or leaves
 *  them at the defaults) and calls StartGame() from the button's OnClicked.
 */
UCLASS()
class FPS_API UShooterLoginMenu : public UUserWidget
{
	GENERATED_BODY()

public:

	/** DS server address (defaults to the local machine). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Login")
	FString ServerAddress = TEXT("127.0.0.1");

	/** DS server port (default UE dedicated server port). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Login", meta = (ClampMin = 1, ClampMax = 65535))
	int32 ServerPort = 7777;

	/** Connects to the DS: builds "IP:Port" from ServerAddress/ServerPort and
	 *  travels (client-side OpenLevel). Defaults to 127.0.0.1:7777. Call from
	 *  the "start game" button's OnClicked. */
	UFUNCTION(BlueprintCallable, Category = "Login")
	void StartGame();

	/** Blueprint hook fired as the connection is being attempted. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void BP_OnLoginStarted();

	/** Blueprint hook fired if the connection fails (reserved — currently the
	 *  engine's default "failed to connect" flow applies). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Login")
	void BP_OnLoginFailed();
};
