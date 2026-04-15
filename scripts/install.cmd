@echo off
:: ============================================
:: RE9 Head Tracking - Install
:: ============================================
:: REFramework plugin for Resident Evil Requiem
:: ============================================

:: --- CONFIG BLOCK ---
set "MOD_DISPLAY_NAME=RE9 Head Tracking"
set "GAME_EXE=re9.exe"
set "GAME_DISPLAY_NAME=Resident Evil Requiem"
set "STEAM_FOLDER_NAME=RESIDENT EVIL requiem BIOHAZARD requiem"
set "ENV_VAR_NAME=RE9_PATH"
set "MOD_FILES=RE9HeadTracking.dll HeadTracking.ini"
set "MOD_INTERNAL_NAME=RE9HeadTracking"
set "MOD_VERSION=0.1.1"
set "STATE_FILE=.headtracking-state.json"
set "MOD_CONTROLS=Controls:&echo   Home - Recenter head tracking&echo   End  - Toggle head tracking on/off&echo   PgUp - Toggle position tracking&echo   Ins  - Toggle reticle"
set "REFRAMEWORK_URL=https://github.com/praydog/REFramework-nightly/releases/latest/download/RE9-nightly.zip"
:: --- END CONFIG BLOCK ---

call :main %*
set "_EC=%errorlevel%"
echo.
pause
exit /b %_EC%

:main
setlocal enabledelayedexpansion

echo.
echo === %MOD_DISPLAY_NAME% - Install ===
echo.

set "SCRIPT_DIR=%~dp0"
set "GAME_PATH="

:: --- Find game path ---

:: Check command line argument
if not "%~1"=="" (
    if exist "%~1\%GAME_EXE%" (
        set "GAME_PATH=%~1"
        goto :found_game
    )
    echo ERROR: %GAME_EXE% not found at: %~1
    echo.
    exit /b 1
)

:: Check environment variable
if defined %ENV_VAR_NAME% (
    call set "_ENV_PATH=%%%ENV_VAR_NAME%%%"
    if exist "!_ENV_PATH!\%GAME_EXE%" (
        set "GAME_PATH=!_ENV_PATH!"
        goto :found_game
    )
)

:: Search Steam
call :find_steam_game
if defined GAME_PATH goto :found_game

echo ERROR: Could not find %GAME_DISPLAY_NAME% installation.
echo.
echo Please either:
echo   1. Pass the game path as an argument:
echo      install.cmd "C:\path\to\%GAME_DISPLAY_NAME%"
echo.
echo   2. Set the %ENV_VAR_NAME% environment variable:
echo      set %ENV_VAR_NAME%=C:\path\to\%GAME_DISPLAY_NAME%
echo.
exit /b 1

:found_game
echo Found %GAME_DISPLAY_NAME% at: %GAME_PATH%
echo.

:: Check if game is running
tasklist /FI "IMAGENAME eq %GAME_EXE%" 2>NUL | find /I "%GAME_EXE%" >NUL 2>&1
if %errorlevel%==0 (
    echo WARNING: %GAME_DISPLAY_NAME% is currently running.
    echo Please close the game before installing.
    echo.
    exit /b 1
)

:: --- Check/Install REFramework ---
set "REFRAMEWORK_DLL=%GAME_PATH%\dinput8.dll"
if not exist "%REFRAMEWORK_DLL%" (
    echo REFramework not found. Downloading...
    echo.

    set "TEMP_ZIP=%TEMP%\reframework_re9.zip"

    :: Resolve latest nightly URL via GitHub API
    echo Resolving latest REFramework nightly...
    for /f "delims=" %%U in ('powershell -Command "(Invoke-RestMethod 'https://api.github.com/repos/praydog/REFramework-nightly/releases/latest').assets | Where-Object { $_.name -eq 'RE9.zip' } | Select-Object -ExpandProperty browser_download_url"') do set "RESOLVED_URL=%%U"

    if not defined RESOLVED_URL (
        echo ERROR: Could not resolve REFramework download URL.
        echo Please install REFramework manually from:
        echo   https://www.nexusmods.com/residentevilrequiem/mods
        echo.
        exit /b 1
    )

    echo Downloading from: !RESOLVED_URL!
    curl -fL -o "!TEMP_ZIP!" "!RESOLVED_URL!"
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to download REFramework.
        echo Please install REFramework manually from:
        echo   https://www.nexusmods.com/residentevilrequiem/mods
        echo.
        exit /b 1
    )

    echo Extracting REFramework...
    powershell -Command "Expand-Archive -Path '!TEMP_ZIP!' -DestinationPath '%GAME_PATH%' -Force"
    del "!TEMP_ZIP!" 2>NUL

    if not exist "%REFRAMEWORK_DLL%" (
        echo ERROR: REFramework installation failed.
        exit /b 1
    )

    echo REFramework installed successfully.
    echo.

    :: Record that we installed REFramework
    echo {"installed_by_us": {"reframework": true}} > "%GAME_PATH%\%STATE_FILE%"
) else (
    echo REFramework found.
)

:: --- Install mod files ---
set "PLUGINS_DIR=%GAME_PATH%\reframework\plugins"
if not exist "%PLUGINS_DIR%" (
    mkdir "%PLUGINS_DIR%"
)

echo.
echo Installing mod files...

for %%F in (%MOD_FILES%) do (
    if exist "%SCRIPT_DIR%plugins\%%F" (
        copy /Y "%SCRIPT_DIR%plugins\%%F" "%PLUGINS_DIR%\%%F" >NUL
        echo   Copied: %%F
    ) else if exist "%SCRIPT_DIR%%%F" (
        copy /Y "%SCRIPT_DIR%%%F" "%PLUGINS_DIR%\%%F" >NUL
        echo   Copied: %%F
    ) else (
        echo   WARNING: %%F not found in installer package
    )
)

:: Update state file
if not exist "%GAME_PATH%\%STATE_FILE%" (
    echo {"installed_by_us": {}} > "%GAME_PATH%\%STATE_FILE%"
)

echo.
echo ============================================
echo  %MOD_DISPLAY_NAME% v%MOD_VERSION% installed!
echo ============================================
echo.
echo %MOD_CONTROLS%
echo.
echo Make sure OpenTrack is running and sending
echo data to UDP port 4242.
echo.

exit /b 0

:: --- Steam game detection subroutine ---
:find_steam_game
set "STEAM_PATH="

:: Try registry
for /f "tokens=2*" %%A in ('reg query "HKLM\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>NUL') do set "STEAM_PATH=%%B"
if not defined STEAM_PATH (
    for /f "tokens=2*" %%A in ('reg query "HKLM\SOFTWARE\Valve\Steam" /v InstallPath 2^>NUL') do set "STEAM_PATH=%%B"
)
if not defined STEAM_PATH exit /b 0

:: Check default library
set "_CHECK=%STEAM_PATH%\steamapps\common\%STEAM_FOLDER_NAME%"
if exist "!_CHECK!\%GAME_EXE%" (
    set "GAME_PATH=!_CHECK!"
    exit /b 0
)

:: Parse libraryfolders.vdf for additional libraries
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
