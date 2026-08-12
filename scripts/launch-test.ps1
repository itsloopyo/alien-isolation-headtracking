[CmdletBinding()]
param(
    [string]$GamePath,
    [int]$WaitSeconds = 30
)
$ErrorActionPreference = 'Stop'

if (-not $GamePath) {
    $candidates = @(
        $env:ALIEN_ISOLATION_PATH,
        'C:\Program Files (x86)\Steam\steamapps\common\Alien Isolation',
        'D:\SteamLibrary\steamapps\common\Alien Isolation'
    ) | Where-Object { $_ }
    $GamePath = $candidates | Where-Object { Test-Path (Join-Path $_ 'AI.exe') } | Select-Object -First 1
}
if (-not $GamePath) { throw "Could not locate Alien: Isolation." }

$log = Join-Path $GamePath 'AlienIsolationHeadTracking.log'
if (Test-Path $log) { Remove-Item $log -Force }

Write-Host "Launching Alien: Isolation via Steam..." -ForegroundColor Cyan
Start-Process "steam://run/214490"

$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 2
    if (Get-Process -Name 'AI' -ErrorAction SilentlyContinue) { break }
}
Start-Sleep -Seconds $WaitSeconds

$proc = Get-Process -Name 'AI' -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "Stopping AI.exe (pid $($proc.Id))..." -ForegroundColor Cyan
    Stop-Process -Id $proc.Id -Force
    Start-Sleep -Seconds 2
}

Write-Host "`n===== $log =====" -ForegroundColor Yellow
if (Test-Path $log) {
    Get-Content $log
} else {
    Write-Host "LOG NOT FOUND - plugin did not load." -ForegroundColor Red
    exit 1
}
