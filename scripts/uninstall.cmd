@echo off
:: ============================================
:: RE9 Head Tracking - Uninstall
:: ============================================
:: Based on cameraunlock-core/scripts/templates/uninstall.cmd.
:: Detection delegated to shared/find-game.ps1 (reads games.json).
:: REFramework variant: loader is dinput8.dll + reframework/, mod files
:: live in reframework/plugins/.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=resident-evil-requiem"
set "MOD_DISPLAY_NAME=RE9 Head Tracking"
set "MOD_DLLS=RE9HeadTracking.dll HeadTracking.ini"
set "MOD_INTERNAL_NAME=RE9HeadTracking"
set "STATE_FILE=.headtracking-state.json"
set "LEGACY_DLLS=RE9HeadTracking.dll.bak"
:: --- END CONFIG BLOCK ---

call :main %*
set "_EC=%errorlevel%"
echo.
pause
exit /b %_EC%

:main
setlocal enabledelayedexpansion

echo.
echo === %MOD_DISPLAY_NAME% - Uninstall ===
echo.

set "SCRIPT_DIR=%~dp0"
set "FORCE=0"

set "_GIVEN_PATH="
:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="/force" ( set "FORCE=1" & shift & goto :parse_args )
if /i "%~1"=="--force" ( set "FORCE=1" & shift & goto :parse_args )
if not defined _GIVEN_PATH (
    if exist "%~1\" (
        set "_GIVEN_PATH=%~1"
        shift
        goto :parse_args
    )
)
shift
goto :parse_args

:args_done

:: --- Resolve game path via shared shim ---
:: Release ZIP layout: scripts\ is the ZIP root, shim is at shared\find-game.ps1.
:: Dev tree layout: scripts\ is <repo>\scripts\, shim is at ..\cameraunlock-core\scripts\find-game.ps1.
set "_SHIM=%SCRIPT_DIR%shared\find-game.ps1"
if not exist "%_SHIM%" set "_SHIM=%SCRIPT_DIR%..\cameraunlock-core\scripts\find-game.ps1"
if not exist "%_SHIM%" (
    echo ERROR: find-game.ps1 not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, make sure the cameraunlock-core submodule is checked out.
    exit /b 1
)
set "_SHIM_OUT=%TEMP%\cul-find-%RANDOM%-%RANDOM%.cmd"
set "_GIVEN_ARG="
if defined _GIVEN_PATH set "_GIVEN_ARG=-GivenPath "!_GIVEN_PATH!""
powershell -NoProfile -ExecutionPolicy Bypass -File "%_SHIM%" -GameId %GAME_ID% -OutFile "!_SHIM_OUT!" !_GIVEN_ARG!
set "_PS_EC=!errorlevel!"
if not "!_PS_EC!"=="0" (
    echo.
    echo ERROR: Could not resolve game install path ^(shim exit code !_PS_EC!^).
    echo Pass a path explicitly: uninstall.cmd "C:\path\to\game"
    echo.
    del "!_SHIM_OUT!" 2>nul
    exit /b 1
)
call "!_SHIM_OUT!"
del "!_SHIM_OUT!" 2>nul

echo Found %GAME_DISPLAY_NAME% at: %GAME_PATH%
echo.

:: --- Remove mod files ---
set "PLUGINS_DIR=%GAME_PATH%\reframework\plugins"

echo Removing mod files...
set "REMOVED=0"
for %%F in (%MOD_DLLS%) do (
    if exist "!PLUGINS_DIR!\%%F" (
        del "!PLUGINS_DIR!\%%F"
        echo   Removed: %%F
        set /a REMOVED+=1
    )
)

if defined LEGACY_DLLS (
    for %%F in (%LEGACY_DLLS%) do (
        if exist "!PLUGINS_DIR!\%%F" (
            del "!PLUGINS_DIR!\%%F"
            echo   Removed: %%F ^(legacy^)
            set /a REMOVED+=1
        )
    )
)

if "!REMOVED!"=="0" echo   No mod files found

:: --- Decide whether to remove REFramework ---
set "REMOVE_REFRAMEWORK=0"
if "!FORCE!"=="1" set "REMOVE_REFRAMEWORK=1"

if "!REMOVE_REFRAMEWORK!"=="0" (
    if exist "!GAME_PATH!\%STATE_FILE%" (
        findstr /c:"installed_by_us" "!GAME_PATH!\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
        if not errorlevel 1 set "REMOVE_REFRAMEWORK=1"
    )
)

if "!REMOVE_REFRAMEWORK!"=="1" (
    echo.
    echo Removing REFramework...
    if exist "!GAME_PATH!\dinput8.dll" (
        del "!GAME_PATH!\dinput8.dll"
        echo   Removed: dinput8.dll
    )
    if exist "!GAME_PATH!\reframework" (
        rmdir /S /Q "!GAME_PATH!\reframework"
        echo   Removed: reframework/
    )
) else (
    echo.
    echo REFramework was not installed by this mod - leaving intact. Use --force to remove anyway.
)

:: --- Clean up state file ---
if exist "!GAME_PATH!\%STATE_FILE%" (
    del "!GAME_PATH!\%STATE_FILE%"
    echo   Removed: %STATE_FILE%
)

echo.
echo ============================================
echo  %MOD_DISPLAY_NAME% uninstalled.
echo ============================================
echo.

exit /b 0
