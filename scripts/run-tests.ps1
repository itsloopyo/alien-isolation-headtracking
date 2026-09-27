#Requires -Version 5.1
<#
.SYNOPSIS
    Checks what the config differential test compiles against its recorded
    provenance, then runs every test in the build tree.
.NOTES
    Run via: pixi run test (which builds first)
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot

# The differential test is only as good as its claim about what it compiled.
# SHA256 through .NET, because Get-FileHash is not found when a runner's pwsh
# runs this under Windows PowerShell.
$provenance = Join-Path $projectRoot 'tests/config_differential/provenance.txt'
$sha256 = [System.Security.Cryptography.SHA256]::Create()
foreach ($line in Get-Content $provenance) {
    if ($line -match '^\s*(#|$)') { continue }
    $hash, $path = ($line -split '\s+', 3)[0, 1]
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $projectRoot $path))
    $actual = -join ($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
    if ($actual -ne $hash) { throw "$path has changed: sha256 $actual, provenance.txt records $hash" }
}

& ctest --test-dir (Join-Path $projectRoot 'build') -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'tests failed' }
