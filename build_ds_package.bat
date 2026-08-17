@echo off
REM ============================================================
REM  FPS - Dedicated Server Package Launcher
REM  Double-click to run, or execute from command line.
REM  Output: D:\UnrealProject\FPS\PackageDS\  (FPS-Server.exe)
REM ============================================================

echo [1/2] Cleaning stale log locks...

REM Clean UAT log locks (stale locks make next launch hang/crash)
del /Q "%APPDATA%\Unreal Engine\AutomationTool\Logs\D+Program+Files+(x86)+UE_5.8\Log.txt" 2>nul
del /Q "%APPDATA%\Unreal Engine\AutomationTool\Logs\D+Program+Files+(x86)+UE_5.8\Log.json" 2>nul
del /Q "%APPDATA%\Unreal Engine\AutomationTool\Logs\D+Program+Files+(x86)+UE_5.8\UBA-FPS-Win64-Development.txt" 2>nul

REM Clean UBT log locks (UBT crashes if it cannot back up old logs)
del /Q "%LOCALAPPDATA%\UnrealBuildTool\Log.txt" 2>nul
del /Q "%LOCALAPPDATA%\UnrealBuildTool\Log.json" 2>nul
del /Q "%LOCALAPPDATA%\UnrealBuildTool\Trace*.uba" 2>nul

echo [2/2] Launching DS package in a new minimized window...
start "FPS_DS_Package" /min cmd /c D:\UnrealProject\FPS\build_ds_core.bat

echo.
echo Launched. Log: D:\UnrealProject\FPS\package_ds_log.txt
echo Result: D:\UnrealProject\FPS\package_ds_exit.txt
echo Output: D:\UnrealProject\FPS\PackageDS\
echo.
echo NOTE: UAT is single-instance - do NOT launch twice!
pause
