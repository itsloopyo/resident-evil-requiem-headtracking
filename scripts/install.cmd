@echo off
:: ============================================
:: Resident Evil Requiem - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-reframework.cmd.

:: --- CONFIG BLOCK ---
set "GAME_ID=resident-evil-requiem"
set "MOD_DISPLAY_NAME=RE9 Head Tracking"
set "MOD_DLLS=RE9HeadTracking.dll HeadTracking.ini"
set "MOD_INTERNAL_NAME=RE9HeadTracking"
set "MOD_VERSION=0.2.2"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=REFramework"
set "REFRAMEWORK_VENDOR_ZIP_NAME=REFramework.zip"
set "MOD_CONTROLS=Controls:&echo   Home - Recenter head tracking&echo   End  - Toggle head tracking on/off&echo   PgUp - Toggle position tracking&echo   Ins  - Toggle reticle"
:: --- END CONFIG BLOCK ---

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-reframework.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-reframework.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-reframework.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%