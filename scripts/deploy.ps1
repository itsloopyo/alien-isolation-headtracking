#!/usr/bin/env pwsh
#Requires -Version 5.1
# Deploy the built .asi (and the ASI loader, if absent) into a local
# Alien: Isolation install. No prompts: exits non-zero when detection or the
# copy fails.
# Usage: pixi run install   |   powershell -File scripts/deploy.ps1 "<game path>"

[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [string]$GamePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Import-Module (Join-Path $root 'cameraunlock-core\powershell\GamePathDetection.psm1') -Force

# Same resolution order as install.cmd: explicit path, then env var / registry
# / Steam library / games.json via the shared detector.
if ($GamePath) {
    if (-not (Test-Path (Join-Path $GamePath 'AI.exe'))) {
        Write-Error "AI.exe not found under: $GamePath"
        exit 1
    }
} else {
    $GamePath = Find-GamePath -GameId 'alien-isolation'
    if (-not $GamePath) {
        Write-Error "Could not locate Alien: Isolation. Set ALIEN_ISOLATION_PATH or pass the install path as the first argument."
        exit 1
    }
}

$asi = Join-Path $root 'build\Release\AlienIsolationHeadTracking.asi'
if (-not (Test-Path $asi)) {
    Write-Error "Build output not found: $asi (run 'pixi run build' first)."
    exit 1
}

# AI.exe must be proxied via xinput1_3.dll: winmm is defeated by the KnownDLLs
# dependency closure (resolves from System32, ignoring our app-dir copy), and a
# dxgi proxy crashes D3D init. xinput1_3 is an AI import, outside KnownDLLs, and
# non-rendering, so the app-dir proxy wins and is safe.
$loaderDest = Join-Path $GamePath 'xinput1_3.dll'
if (-not (Test-Path $loaderDest)) {
    $loader = Join-Path $root 'vendor\ultimate-asi-loader\dinput8.dll'
    if (-not (Test-Path $loader)) {
        Write-Error "ASI loader not vendored at $loader. Run 'pixi run update-deps' first."
        exit 1
    }
    Copy-Item $loader $loaderDest -Force
    Write-Host "Deployed ASI loader -> xinput1_3.dll" -ForegroundColor Green
} else {
    Write-Host "xinput1_3.dll already present, leaving it." -ForegroundColor DarkGray
}

Copy-Item $asi (Join-Path $GamePath 'AlienIsolationHeadTracking.asi') -Force
Write-Host "Deployed AlienIsolationHeadTracking.asi -> $GamePath" -ForegroundColor Green
