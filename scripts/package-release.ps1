#!/usr/bin/env pwsh
#Requires -Version 5.1
# Packaging for Alien: Isolation Head Tracking (C++ / Ultimate ASI Loader).
# Produces two ZIPs in release/:
#   - AlienIsolationHeadTracking-v{version}-installer.zip (GitHub Release)
#   - AlienIsolationHeadTracking-v{version}-nexus.zip     (extract to game folder)
#
# Consumes vendor/ultimate-asi-loader/ exactly as committed. Bumping the
# vendored loader is `pixi run update-deps`, a separate deliberate action.
# No prompts, no network: exits non-zero on any failure.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = $PSScriptRoot
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force

$modName = 'AlienIsolationHeadTracking'

$cmakeText = Get-Content (Join-Path $projectDir 'CMakeLists.txt') -Raw
if ($cmakeText -notmatch "project\($modName VERSION (\d+\.\d+\.\d+)") {
    throw "Could not parse version from CMakeLists.txt"
}
$version = $Matches[1]

Write-Host ""
Write-Host "=== Packaging $modName v$version ===" -ForegroundColor Magenta
Write-Host ""

$releaseDir = Join-Path $projectDir 'release'
if (-not (Test-Path $releaseDir)) { New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null }

$asiPath = Join-Path $projectDir "build\Release\$modName.asi"
if (-not (Test-Path $asiPath)) {
    throw "$modName.asi not found at: $asiPath. Run 'pixi run build' first."
}

$vendorAsiDir = Join-Path $projectDir 'vendor\ultimate-asi-loader'
if (-not (Test-Path (Join-Path $vendorAsiDir 'dinput8.dll'))) {
    throw "Bundled ASI loader missing: $vendorAsiDir\dinput8.dll. Run 'pixi run update-deps' first."
}

foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    if (-not (Test-Path (Join-Path $scriptDir $s))) { throw "Required script not found: $s" }
}

$launcherManifestPath = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $launcherManifestPath)) {
    throw "launcher-manifest.json not found at: $launcherManifestPath"
}

# --- Installer ZIP -----------------------------------------------------
Write-Host '--- Installer ZIP ---' -ForegroundColor Yellow

$ghStaging = Join-Path $releaseDir 'staging-installer'
if (Test-Path $ghStaging) { Remove-Item -Recurse -Force $ghStaging }
New-Item -ItemType Directory -Path $ghStaging -Force | Out-Null

foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    Copy-Item (Join-Path $scriptDir $s) -Destination $ghStaging -Force
}

# install.cmd resolves the game through shared\find-game.ps1 on every run, so
# the bundle is mandatory whenever install.cmd ships. -NoRefresh keeps
# packaging offline and deterministic: the shipped bundle is whatever the
# committed submodule pointer holds, identical locally and in CI.
Copy-SharedBundle -StagingDir $ghStaging -CoreRoot (Join-Path $projectDir 'cameraunlock-core') -NoRefresh

$pluginsDir = Join-Path $ghStaging 'plugins'
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $asiPath -Destination $pluginsDir -Force
Write-Host "  plugins/$modName.asi" -ForegroundColor Green

# Ultimate ASI Loader ships as the raw dinput8.dll; install.cmd copies it to
# xinput1_3.dll next to AI.exe. LICENSE travels for MIT attribution.
$ghVendorDir = Join-Path $ghStaging 'vendor\ultimate-asi-loader'
New-Item -ItemType Directory -Path $ghVendorDir -Force | Out-Null
foreach ($vendorFile in @('dinput8.dll', 'LICENSE', 'README.md')) {
    $src = Join-Path $vendorAsiDir $vendorFile
    if (-not (Test-Path $src)) {
        throw "Vendored ASI loader is incomplete: $vendorAsiDir\$vendorFile is missing. Run 'pixi run update-deps' and commit the result."
    }
    Copy-Item $src -Destination $ghVendorDir -Force
}
Write-Host "  vendor/ultimate-asi-loader/" -ForegroundColor Green

foreach ($doc in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
    $p = Join-Path $projectDir $doc
    if (-not (Test-Path $p)) { throw "Required document not found: $doc" }
    Copy-Item -Path $p -Destination $ghStaging -Force
}

# Stamp mod_info.version from the build so the shipped manifest can never
# disagree with the built .asi. mod_info.version is the only semver in the file.
$manifestText = Get-Content $launcherManifestPath -Raw
$manifestText = $manifestText -replace '("version":\s*")\d+\.\d+\.\d+(")', "`${1}$version`$2"
[System.IO.File]::WriteAllText(
    (Join-Path $ghStaging 'launcher-manifest.json'),
    $manifestText,
    (New-Object System.Text.UTF8Encoding $false))
Write-Host "  launcher-manifest.json (version $version)" -ForegroundColor Green

$installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
if (Test-Path $installerZip) { Remove-Item $installerZip -Force }
Push-Location $ghStaging
try { Compress-Archive -Path '.\*' -DestinationPath $installerZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $ghStaging

Write-Host ("  $installerZip ({0:N1} KB)" -f ((Get-Item $installerZip).Length / 1KB)) -ForegroundColor Green

# --- Nexus ZIP ---------------------------------------------------------
Write-Host ''
Write-Host '--- Nexus ZIP ---' -ForegroundColor Yellow

# AI.exe sits at the game root, so the .asi deploy path is the root itself.
# Nexus users manage their own ASI loader - never ship the vendored DLL here.
$nexusStaging = Join-Path $releaseDir 'staging-nexus'
if (Test-Path $nexusStaging) { Remove-Item -Recurse -Force $nexusStaging }
New-Item -ItemType Directory -Path $nexusStaging -Force | Out-Null

Copy-Item $asiPath -Destination $nexusStaging -Force

# The .asi statically links MinHook (BSD-2-Clause) and cameraunlock-core (MIT),
# and our own MIT terms require the notice to travel with any copy - so both
# documents ship even though this ZIP carries no loader.
foreach ($doc in @('LICENSE', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item (Join-Path $projectDir $doc) -Destination $nexusStaging -Force
}

$nexusZip = Join-Path $releaseDir "$modName-v$version-nexus.zip"
if (Test-Path $nexusZip) { Remove-Item $nexusZip -Force }
Push-Location $nexusStaging
try { Compress-Archive -Path '.\*' -DestinationPath $nexusZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $nexusStaging

Write-Host ("  $nexusZip ({0:N1} KB)" -f ((Get-Item $nexusZip).Length / 1KB)) -ForegroundColor Green

Write-Host ''
Write-Host '=== Package Complete ===' -ForegroundColor Magenta

Write-Output $installerZip
Write-Output $nexusZip
