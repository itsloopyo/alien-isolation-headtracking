@echo off
:: ============================================
:: Alien: Isolation Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-asi.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper. Everything below the
:: CONFIG BLOCK is copied verbatim from
:: cameraunlock-core/scripts/templates/install-wrapper-asi.cmd.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=alien-isolation"
set "MOD_DISPLAY_NAME=Alien: Isolation Head Tracking"
set "MOD_DLLS=AlienIsolationHeadTracking.asi"
set "MOD_INTERNAL_NAME=AlienIsolationHeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
set "ASI_LOADER_NAME=xinput1_3.dll"
set "MOD_CONTROLS=Controls: End=toggle  PageUp=tracking-mode  PageDown=yaw-mode  Insert=frustum-widen  Delete=injection-mode  (chords: Ctrl+Shift+Y/G/H/U/J)"
:: --- END CONFIG BLOCK ---

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-asi.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-asi.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-asi.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
