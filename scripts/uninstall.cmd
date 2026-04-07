@echo off
:: ============================================
:: RE9 Head Tracking - Uninstall
:: ============================================

:: --- CONFIG BLOCK ---
set "MOD_DISPLAY_NAME=RE9 Head Tracking"
set "GAME_EXE=re9.exe"
set "GAME_DISPLAY_NAME=Resident Evil Requiem"
set "STEAM_FOLDER_NAME=RESIDENT EVIL requiem BIOHAZARD requiem"
set "ENV_VAR_NAME=RE9_PATH"
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
set "GAME_PATH="
set "FORCE_MODE=0"

:: Check for --force flag
if "%~1"=="--force" (
    set "FORCE_MODE=1"
    shift
)

:: --- Find game path ---
if not "%~1"=="" (
    if exist "%~1\%GAME_EXE%" (
        set "GAME_PATH=%~1"
        goto :found_game
    )
)

if defined %ENV_VAR_NAME% (
    call set "_ENV_PATH=%%%ENV_VAR_NAME%%%"
    if exist "!_ENV_PATH!\%GAME_EXE%" (
        set "GAME_PATH=!_ENV_PATH!"
        goto :found_game
    )
)

call :find_steam_game
if defined GAME_PATH goto :found_game

echo ERROR: Could not find %GAME_DISPLAY_NAME% installation.
exit /b 1

:found_game
echo Found %GAME_DISPLAY_NAME% at: %GAME_PATH%
echo.

set "PLUGINS_DIR=%GAME_PATH%\reframework\plugins"

:: Remove mod files
:: Use !VAR! (delayed expansion) for paths containing parentheses
:: e.g. "Program Files (x86)" — the ) breaks %VAR% inside ( blocks.
echo Removing mod files...
for %%F in (%MOD_DLLS%) do (
    if exist "!PLUGINS_DIR!\%%F" (
        del "!PLUGINS_DIR!\%%F"
        echo   Removed: %%F
    )
)

:: Remove legacy/backup files
for %%F in (%LEGACY_DLLS%) do (
    if exist "!PLUGINS_DIR!\%%F" (
        del "!PLUGINS_DIR!\%%F"
        echo   Removed: %%F
    )
)

:: Check if we should remove REFramework
set "REMOVE_REFRAMEWORK=0"
if "%FORCE_MODE%"=="1" set "REMOVE_REFRAMEWORK=1"

if "!REMOVE_REFRAMEWORK!"=="0" (
    if exist "!GAME_PATH!\%STATE_FILE%" (
        findstr /C:"reframework" "!GAME_PATH!\%STATE_FILE%" >NUL 2>&1
        if !errorlevel!==0 (
            set "REMOVE_REFRAMEWORK=1"
        )
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
)

:: Clean up state file
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

:: --- Steam detection (same as install.cmd) ---
:find_steam_game
set "STEAM_PATH="
for /f "tokens=2*" %%A in ('reg query "HKLM\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>NUL') do set "STEAM_PATH=%%B"
if not defined STEAM_PATH (
    for /f "tokens=2*" %%A in ('reg query "HKLM\SOFTWARE\Valve\Steam" /v InstallPath 2^>NUL') do set "STEAM_PATH=%%B"
)
if not defined STEAM_PATH exit /b 0

set "_CHECK=%STEAM_PATH%\steamapps\common\%STEAM_FOLDER_NAME%"
if exist "!_CHECK!\%GAME_EXE%" (
    set "GAME_PATH=!_CHECK!"
    exit /b 0
)

set "VDF=%STEAM_PATH%\steamapps\libraryfolders.vdf"
if not exist "%VDF%" exit /b 0

for /f "tokens=1,2 delims=	 " %%A in ('findstr /C:"path" "%VDF%"') do (
    set "_LIB=%%~B"
    set "_LIB=!_LIB:"=!"
    if exist "!_LIB!\steamapps\common\%STEAM_FOLDER_NAME%\%GAME_EXE%" (
        set "GAME_PATH=!_LIB!\steamapps\common\%STEAM_FOLDER_NAME%"
        exit /b 0
    )
)

exit /b 0
