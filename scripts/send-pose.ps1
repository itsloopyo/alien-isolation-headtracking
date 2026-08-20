[CmdletBinding()]
param(
    [double]$Yaw = 0,
    [double]$Pitch = 0,
    [double]$Roll = 0,
    [double]$X = 0,
    [double]$Y = 0,
    [double]$Z = 0,
    [int]$Seconds = 20,
    [int]$WarmupSeconds = 0,
    [int]$Port = 4242,
    # The mod's receiver locks onto the FIRST source that sends to the port and
    # ignores every other one, so a tracker app already running owns it and
    # nothing here gets through. Starting this before the game binds, at an
    # interval short enough to win the race, is how a scripted pose takes the
    # lock instead.
    [int]$IntervalMs = 16,
    # Seconds to spend sending flat out at the start, with no sleep between
    # datagrams. Start-Sleep cannot pace below the ~15ms system timer tick, so a
    # tracker app sending at 60Hz wins the source lock about half the time even
    # at -IntervalMs 2. A spin loop sends thousands a second and wins it every
    # time; it only has to last long enough to cover the game's bind.
    [int]$BurstSeconds = 0
)
$ErrorActionPreference = 'Stop'

# OpenTrack wire format: six little-endian doubles, x y z yaw pitch roll.
$client = New-Object System.Net.Sockets.UdpClient
$client.Connect('127.0.0.1', $Port)
$bytes = New-Object byte[] 48
$vals = @($X, $Y, $Z, $Yaw, $Pitch, $Roll)
for ($i = 0; $i -lt 6; $i++) {
    [BitConverter]::GetBytes([double]$vals[$i]).CopyTo($bytes, $i * 8)
}

# Warm up with neutral on the SAME stream before the target pose, so the
# smoothing and the interpolator settle on a known baseline first. Opening
# straight onto the target makes the first frames a blend of nothing and the
# target, which reads as a slower ramp rather than a clean step.
$neutral = New-Object byte[] 48
$deadline = (Get-Date).AddSeconds($WarmupSeconds)
while ((Get-Date) -lt $deadline) {
    [void]$client.Send($neutral, $neutral.Length)
    Start-Sleep -Milliseconds $IntervalMs
}

if ($BurstSeconds -gt 0) {
    $burstEnd = (Get-Date).AddSeconds($BurstSeconds)
    while ((Get-Date) -lt $burstEnd) { [void]$client.Send($bytes, $bytes.Length) }
}

$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    [void]$client.Send($bytes, $bytes.Length)
    Start-Sleep -Milliseconds $IntervalMs
}
$client.Close()
Write-Host "sent pose yaw=$Yaw pitch=$Pitch roll=$Roll for ${Seconds}s (warmup ${WarmupSeconds}s)"
