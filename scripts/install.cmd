@echo off
:: ============================================
:: RE9 Head Tracking - Install
:: ============================================
:: Based on cameraunlock-core/scripts/templates/install.cmd.
:: Detection delegated to shared/find-game.ps1 (reads games.json).
:: REFramework variant: loader is dinput8.dll + reframework/, mod files
:: go to reframework/plugins/. MOD_DLLS here also carries a .ini since
:: HeadTracking's config is plaintext, not C# attrs - deploy/remove
:: loops treat entries as opaque filenames.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=resident-evil-requiem"
set "MOD_DISPLAY_NAME=RE9 Head Tracking"
set "MOD_DLLS=RE9HeadTracking.dll HeadTracking.ini"
set "MOD_INTERNAL_NAME=RE9HeadTracking"
set "MOD_VERSION=0.1.2"
set "STATE_FILE=.headtracking-state.json"
set "MOD_CONTROLS=Controls:&echo   Home - Recenter head tracking&echo   End  - Toggle head tracking on/off&echo   PgUp - Toggle position tracking&echo   Ins  - Toggle reticle"
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
if not "%~1"=="" set "_GIVEN_ARG=-GivenPath "%~1""
powershell -NoProfile -ExecutionPolicy Bypass -File "%_SHIM%" -GameId %GAME_ID% -OutFile "%_SHIM_OUT%" %_GIVEN_ARG%
set "_PS_EC=!errorlevel!"
if not "!_PS_EC!"=="0" (
    echo.
    echo ERROR: Could not resolve game install path ^(shim exit code !_PS_EC!^).
    echo Pass a path explicitly: install.cmd "C:\path\to\game"
    echo.
    del "!_SHIM_OUT!" 2>nul
    exit /b 1
)
call "!_SHIM_OUT!"
del "!_SHIM_OUT!" 2>nul

echo Found %GAME_DISPLAY_NAME% at: %GAME_PATH%
echo.

:: --- Check if game is running ---
tasklist /FI "IMAGENAME eq %GAME_EXE%" 2>NUL | find /I "%GAME_EXE%" >NUL 2>&1
if %errorlevel%==0 (
    echo WARNING: %GAME_DISPLAY_NAME% is currently running.
    echo Please close the game before installing.
    echo.
    exit /b 1
)

:: --- Check / install REFramework ---
set "WE_INSTALLED_FRAMEWORK=false"
if not exist "%GAME_PATH%\dinput8.dll" (
    echo REFramework not found. Installing...
    echo.
    call :install_reframework
    if errorlevel 1 exit /b 1
    set "WE_INSTALLED_FRAMEWORK=true"
    echo REFramework installed successfully.
    echo.
) else (
    echo Existing REFramework detected, skipping loader install, deploying plugin only.
    :: Preserve installed_by_us from the previous state file, if any.
    if exist "%GAME_PATH%\%STATE_FILE%" (
        findstr /c:"installed_by_us" "%GAME_PATH%\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
        if not errorlevel 1 set "WE_INSTALLED_FRAMEWORK=true"
    )
)

:: --- Install mod files ---
set "PLUGINS_DIR=%GAME_PATH%\reframework\plugins"
if not exist "%PLUGINS_DIR%" mkdir "%PLUGINS_DIR%"

echo.
echo Installing mod files...

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "%SCRIPT_DIR%plugins\%%f" (
        copy /Y "%SCRIPT_DIR%plugins\%%f" "%PLUGINS_DIR%\%%f" >NUL
        echo   Copied: %%f
    ) else if exist "%SCRIPT_DIR%%%f" (
        copy /Y "%SCRIPT_DIR%%%f" "%PLUGINS_DIR%\%%f" >NUL
        echo   Copied: %%f
    ) else (
        echo   ERROR: %%f not found in installer package
        set "DEPLOY_FAILED=1"
    )
)

if "!DEPLOY_FAILED!"=="1" (
    echo.
    echo ========================================
    echo   Deployment Failed!
    echo ========================================
    echo.
    exit /b 1
)

:: --- Update state file ---
> "%GAME_PATH%\%STATE_FILE%" (
    echo {
    echo   "framework": {
    echo     "type": "REFramework",
    echo     "installed_by_us": !WE_INSTALLED_FRAMEWORK!
    echo   },
    echo   "mod": {
    echo     "name": "%MOD_INTERNAL_NAME%",
    echo     "version": "%MOD_VERSION%"
    echo   }
    echo }
)

echo.
echo ============================================
echo  %MOD_DISPLAY_NAME% v%MOD_VERSION% installed!
echo ============================================
echo.
if defined MOD_CONTROLS (
    echo !MOD_CONTROLS!
    echo.
)
echo Make sure OpenTrack is running and sending
echo data to UDP port 4242.
echo.

exit /b 0

:: ============================================
:: Install REFramework (upstream-first, fall back to vendored copy).
:: ============================================
:install_reframework
set "VENDOR_DIR=%SCRIPT_DIR%vendor\reframework"
set "VENDOR_ZIP=%VENDOR_DIR%\RE9.zip"
set "FETCH_SCRIPT=%VENDOR_DIR%\fetch-latest.ps1"
set "TEMP_ZIP=%TEMP%\reframework_re9.zip"
set "LOADER_SOURCE="

if exist "%FETCH_SCRIPT%" (
    echo Trying upstream REFramework nightly...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%FETCH_SCRIPT%" -OutputPath "%TEMP_ZIP%" >nul 2>&1
    if not errorlevel 1 (
        set "LOADER_SOURCE=%TEMP_ZIP%"
        set "USED_UPSTREAM=1"
        echo Using upstream REFramework.
    )
)

if not defined LOADER_SOURCE (
    if not exist "%VENDOR_ZIP%" (
        echo ERROR: Upstream unreachable AND bundled fallback missing at:
        echo   %VENDOR_ZIP%
        echo The installer ZIP is corrupt. Re-download the release.
        exit /b 1
    )
    set "LOADER_SOURCE=%VENDOR_ZIP%"
    echo Upstream unreachable, using bundled fallback copy.
)

echo Extracting REFramework...
powershell -Command "Expand-Archive -Path '!LOADER_SOURCE!' -DestinationPath '%GAME_PATH%' -Force"
if defined USED_UPSTREAM del "!TEMP_ZIP!" 2>NUL

if not exist "%GAME_PATH%\dinput8.dll" (
    echo ERROR: REFramework installation failed.
    exit /b 1
)

exit /b 0
