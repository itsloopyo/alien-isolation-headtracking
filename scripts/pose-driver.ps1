[CmdletBinding()]
param(
    [string]$PoseFile = "$env:TEMP\ai-pose.txt",
    [int]$Seconds = 900,
    # Datagrams per second. Paced by spinning on a stopwatch rather than
    # Start-Sleep, which cannot go below the ~15ms system timer tick - too slow
    # to win the receiver's source lock against a tracker app already running.
    [int]$Hz = 250,
    [int]$Port = 4242
)
$ErrorActionPreference = 'Stop'

# One long-lived socket that keeps sending whatever `PoseFile` says, so a pose
# can be changed from another shell without ever going quiet.
#
# The mod's receiver locks onto the first source that sends to the port and only
# hands over once the incumbent has been SILENT for a second, so a sender that
# exits and restarts loses the lock to whatever tracker app is also running.
# Everything a scripted test needs has to come down one socket that never stops.
#
# PoseFile holds six numbers on one line: x y z yaw pitch roll - the TARGET.
# Missing or malformed, the last good target keeps being sent; going quiet would
# hand the lock away.
#
# Two things the receiver does to a naive sender, both of which look like the mod
# ignoring the pose:
#
#   - A pose repeated bit-for-bit reads as a tracker that has STOPPED, and the
#     receiver freezes on the last pose it believed. So every datagram carries a
#     hundredth of a degree of dither.
#   - A step bigger than 8 degrees is held back until a LATER packet confirms it,
#     which a sender repeating one value can never do. So the pose ramps to the
#     target a fraction of a degree at a time, the way a head moves.

$client = New-Object System.Net.Sockets.UdpClient
$client.Connect('127.0.0.1', $Port)

$target = @(0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
$current = @(0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
$bytes = New-Object byte[] 48

# Degrees (and metres) per datagram. At 250Hz this ramps 90 degrees in under a
# second while every step stays far inside the receiver's 8-degree gate.
$stepAngle = 0.4
$stepPos = 0.004

function Update-Pose {
    if (-not (Test-Path $PoseFile)) { return }
    try {
        $parts = (Get-Content $PoseFile -Raw).Trim() -split '[\s,]+'
        if ($parts.Count -lt 6) { return }
        for ($i = 0; $i -lt 6; $i++) { $script:target[$i] = [double]$parts[$i] }
    } catch { }
}

$log = Join-Path $env:TEMP 'ai-pose-driver.log'
"driver up, pose file $PoseFile" | Out-File $log -Encoding utf8

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$endMs = $Seconds * 1000
$nextReadMs = 0
$nextLogMs = 5000
$sent = 0
$period = 1.0 / $Hz
$nextSend = 0.0
while ($sw.ElapsedMilliseconds -lt $endMs) {
    if ($sw.ElapsedMilliseconds -ge $nextReadMs) {
        $nextReadMs = $sw.ElapsedMilliseconds + 100
        Update-Pose
    }
    for ($i = 0; $i -lt 6; $i++) {
        $step = if ($i -lt 3) { $stepPos } else { $stepAngle }
        $delta = $target[$i] - $current[$i]
        if ([Math]::Abs($delta) -le $step) { $current[$i] = $target[$i] }
        else { $current[$i] += $step * [Math]::Sign($delta) }
    }
    $dither = if ($sent % 2) { 0.01 } else { -0.01 }
    for ($i = 0; $i -lt 6; $i++) {
        $v = $current[$i]
        if ($i -ge 3) { $v += $dither }
        [BitConverter]::GetBytes([double]$v).CopyTo($bytes, $i * 8)
    }
    while ($sw.Elapsed.TotalSeconds -lt $nextSend) { }
    $nextSend += $period
    [void]$client.Send($bytes, $bytes.Length)
    $sent++
    if ($sw.ElapsedMilliseconds -ge $nextLogMs) {
        $nextLogMs = $sw.ElapsedMilliseconds + 5000
        "t=$([int]($sw.ElapsedMilliseconds/1000))s sent=$sent pose=$($current -join ' ') target=$($target -join ' ')" |
            Out-File $log -Append -Encoding utf8
    }
}
$client.Close()
