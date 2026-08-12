# Thin shim. Determine version, delegate to the shared publisher.
# See cameraunlock-core/powershell/NightlyRelease.psm1 for what it does.

[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$versionFile = Join-Path $ProjectRoot 'src\AlienIsolationHeadTracking\version.h'
$versionMatch = Select-String -Path $versionFile -Pattern '#define\s+AIHT_VERSION\s+"([^"]+)"'
if (-not $versionMatch) {
    throw "Could not extract version from $versionFile"
}
$version = $versionMatch.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'alien-isolation' `
    -ModName 'AlienIsolationHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -BuildCommand 'pixi run build' `
    -AllowDirty:$AllowDirty
