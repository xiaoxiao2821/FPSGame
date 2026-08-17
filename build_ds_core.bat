@echo off
REM ============================================================
REM  FPS - Dedicated Server package core command
REM  Launched by build_ds_package.bat in its own window.
REM  Builds the server target (FPS-Server.exe) only.
REM ============================================================

cd /d D:\UnrealProject\FPS

"D:\Program Files (x86)\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project=D:/UnrealProject/FPS/FPS.uproject -noP4 -platform=Win64 -clientconfig=Development -serverconfig=Development -server -noclient -build -cook -map=/Game/Variant_Shooter/Lvl_Shooter.Lvl_Shooter -stage -pak -archive -archivedirectory="D:/UnrealProject/FPS/PackageDS" -nocompileeditor -NoUBA > D:\UnrealProject\FPS\package_ds_log.txt 2>&1

echo PACKAGE_EXIT=%ERRORLEVEL% > D:\UnrealProject\FPS\package_ds_exit.txt
echo.
echo Done. See D:\UnrealProject\FPS\package_ds_exit.txt
pause
