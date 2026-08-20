[CmdletBinding()]
param(
    [int]$WarmupSeconds = 30,
    [string[]]$Keys = @(),
    [int]$KeyDelay = 3,
    [string]$ShotDir = "$env:TEMP\ai-shots",
    [switch]$NoLaunch,
    [int]$SplashSeconds = 50,
    [int]$LoadSeconds = 55,
    [switch]$TabOut,
    [switch]$TestMenu,
    [switch]$ToGame,
    [string]$Shot,
    [string]$AimShot,
    [switch]$Kill
)
$ErrorActionPreference = 'Stop'

$env:LIB = ''
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Native {
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string n);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT {
        public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo;
    }
    [StructLayout(LayoutKind.Explicit, Size = 40)]
    public struct INPUT {
        [FieldOffset(0)] public uint type;
        [FieldOffset(8)] public KEYBDINPUT ki;
    }
    [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, int size);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, IntPtr extra);
    public static void RightDown() { mouse_event(0x0008, 0, 0, 0, IntPtr.Zero); }
    public static void RightUp()   { mouse_event(0x0010, 0, 0, 0, IntPtr.Zero); }

    // The game reads scan codes, so a virtual-key-only synthetic press is
    // ignored. Send scan codes with the extended flag where the key needs it.
    public static void Tap(ushort scan, bool extended) {
        uint SCANCODE = 0x0008, KEYUP = 0x0002, EXT = 0x0001;
        INPUT[] down = new INPUT[1];
        down[0].type = 1;
        down[0].ki.wScan = scan;
        down[0].ki.dwFlags = SCANCODE | (extended ? EXT : 0);
        SendInput(1, down, Marshal.SizeOf(typeof(INPUT)));
        System.Threading.Thread.Sleep(70);
        INPUT[] up = new INPUT[1];
        up[0].type = 1;
        up[0].ki.wScan = scan;
        up[0].ki.dwFlags = SCANCODE | KEYUP | (extended ? EXT : 0);
        SendInput(1, up, Marshal.SizeOf(typeof(INPUT)));
    }
}
"@

New-Item -ItemType Directory -Force -Path $ShotDir | Out-Null

function Get-GameWindow {
    $p = Get-Process -Name 'AI' -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($p) { return $p.MainWindowHandle }
    # MainWindowHandle reads back as 0 while the game is loading.
    return [Native]::FindWindow($null, 'Alien: Isolation')
}

function Save-Shot([string]$tag) {
    # Tracking is suppressed while the game is not foreground, so a shot taken
    # of an unfocused window records the un-rotated view no matter what pose is
    # being sent. Take focus first and give the render thread a moment to
    # produce a frame with the pose applied.
    [void](Set-GameFocus)
    Start-Sleep -Milliseconds 900
    $h = Get-GameWindow
    $r = New-Object Native+RECT
    if ($h -ne [IntPtr]::Zero) { [void][Native]::GetWindowRect($h, [ref]$r) }
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($w -le 0 -or $ht -le 0) {
        # No window handle (loading, or focus lost): fall back to the whole screen.
        $b = [System.Windows.Forms.SystemInformation]::VirtualScreen
        $r.Left = $b.Left; $r.Top = $b.Top; $w = $b.Width; $ht = $b.Height
    }
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $path = Join-Path $ShotDir "$tag.png"
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Host "shot -> $path"
}

# The frontend menus all draw their item list in the same place, so mean
# brightness over that strip separates "title screen" from "a menu is up".
# Closing the loop on this is what stops the navigation desyncing when the
# splash screens take longer than expected.
function Test-MenuListVisible {
    $h = Get-GameWindow
    if ($h -eq [IntPtr]::Zero) { return $false }
    $r = New-Object Native+RECT
    [void][Native]::GetWindowRect($h, [ref]$r)
    if (($r.Right - $r.Left) -le 0) { return $false }
    $bmp = New-Object System.Drawing.Bitmap 220, 50
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left + 176, $r.Top + 755, 0, 0, $bmp.Size)
    # Menu items are white text on a dark planet, so counting near-white pixels
    # separates "list present" from "title screen" far more sharply than a mean.
    $bright = 0
    for ($x = 0; $x -lt 220; $x++) {
        for ($y = 0; $y -lt 50; $y += 2) {
            $p = $bmp.GetPixel($x, $y)
            if ($p.R -gt 200 -and $p.G -gt 200 -and $p.B -gt 200) { $bright++ }
        }
    }
    $g.Dispose(); $bmp.Dispose()
    Write-Host "menu strip bright pixels: $bright"
    return ($bright -gt 60)
}

$scan = @{
    'SPACE' = @(0x39, $false)
    'DOWN'  = @(0x50, $true)
    'UP'    = @(0x48, $true)
    'ENTER' = @(0x1C, $false)
    'ESC'   = @(0x01, $false)
    'E'     = @(0x12, $false)
    'W'     = @(0x11, $false)
    'A'     = @(0x1E, $false)
    'S'     = @(0x1F, $false)
    'D'     = @(0x20, $false)
    'HOME'  = @(0x47, $true)
    'END'   = @(0x4F, $true)
    'DELETE'= @(0x53, $true)
}

# Windows refuses SetForegroundWindow from a background process unless the
# calling thread shares an input queue with the target, so attach to it.
function Set-GameFocus {
    for ($try = 0; $try -lt 8; $try++) {
        $h = Get-GameWindow
        if ($h -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 500; continue }
        if ([Native]::GetForegroundWindow() -eq $h) { return $true }
        $target = [Native]::GetWindowThreadProcessId($h, [IntPtr]::Zero)
        $self = [Native]::GetCurrentThreadId()
        [void][Native]::AttachThreadInput($self, $target, $true)
        [void][Native]::SetForegroundWindow($h)
        [void][Native]::AttachThreadInput($self, $target, $false)
        Start-Sleep -Milliseconds 400
    }
    return ([Native]::GetForegroundWindow() -eq (Get-GameWindow))
}

function Send-Key([string]$name) {
    $focused = Set-GameFocus
    Start-Sleep -Milliseconds 300
    $s = $scan[$name]
    [Native]::Tap([uint16]$s[0], [bool]$s[1])
    Write-Host "sent $name (focused=$focused)"
}

if ($TabOut) {
    # Minimise and restore: the closest scriptable analogue of alt-tabbing out
    # and back, which is when the view was reported flickering between angles.
    $h = Get-GameWindow
    Save-Shot 'tab0-before'
    [void][Native]::ShowWindow($h, 6)      # SW_MINIMIZE
    Start-Sleep -Seconds 6
    [void][Native]::ShowWindow($h, 9)      # SW_RESTORE
    [void](Set-GameFocus)
    # Rapid burst: a flicker only shows as a difference between frames.
    foreach ($n in 1..6) {
        Start-Sleep -Milliseconds 700
        Save-Shot ("tab$n-after")
    }
    exit 0
}

if ($Shot) {
    Save-Shot $Shot
    exit 0
}

# A holstered weapon is not drawn at all, so anything about where the weapon
# sits has to be judged with the aim button held down.
if ($AimShot) {
    [void](Set-GameFocus)
    [Native]::RightDown()
    Start-Sleep -Milliseconds 1200
    Save-Shot $AimShot
    [Native]::RightUp()
    exit 0
}

if ($TestMenu) {
    [void](Test-MenuListVisible)
    exit 0
}

if ($Kill) {
    Get-Process -Name 'AI' -ErrorAction SilentlyContinue | Stop-Process -Force
    Write-Host "killed AI.exe"
    exit 0
}

if (-not $NoLaunch) {
    Get-Process -Name 'AI' -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 2
    $game = 'C:\Program Files (x86)\Steam\steamapps\common\Alien Isolation'
    Remove-Item (Join-Path $game 'AlienIsolationHeadTracking.log') -Force -ErrorAction SilentlyContinue
    Get-ChildItem $ShotDir -Filter *.png -ErrorAction SilentlyContinue | Remove-Item -Force
    Start-Process "steam://run/214490"
    Start-Sleep -Seconds $WarmupSeconds
}

if ($ToGame) {
    $log = 'C:\Program Files (x86)\Steam\steamapps\common\Alien Isolation\AlienIsolationHeadTracking.log'
    $deadline = (Get-Date).AddSeconds(120)
    while ((Get-Date) -lt $deadline -and (Get-GameWindow) -eq [IntPtr]::Zero) { Start-Sleep -Seconds 2 }

    # About 45s of company splashes, then a title screen. Press SPACE until the
    # menu list actually appears rather than trusting a fixed delay.
    Start-Sleep -Seconds $SplashSeconds
    for ($n = 0; $n -lt 12; $n++) {
        if (Test-MenuListVisible) { break }
        Send-Key 'SPACE'
        Start-Sleep -Seconds 4
    }
    Save-Shot 'a-menu'
    if (-not (Test-MenuListVisible)) {
        Save-Shot 'a-FAILED'
        Write-Host 'ABORT: never reached the main menu; sending no confirm keys'
        exit 1
    }

    # Title screen -> Play Game -> Alien: Isolation -> Continue Game.
    #
    # From the main menu the path is deterministic, and every list is walked to
    # its top with UP before selecting, so a missed key cannot shift which item
    # gets confirmed.
    #
    # SAFETY: the third screen's first item is "Start Game", which overwrites
    # the campaign save. UP-to-top then exactly one DOWN lands on "Continue
    # Game". Nothing after this sequence may send a confirm key - if it still
    # desyncs it lands on the "start a new game?" dialog, which defaults to
    # Cancel, so ESC-only recovery can never destroy the save.
    function Send-Keys([string[]]$keys) {
        foreach ($k in $keys) { Send-Key $k; Start-Sleep -Milliseconds 700 }
    }

    Send-Keys @('UP', 'UP', 'UP', 'UP', 'UP')       # -> Play Game
    Send-Key 'SPACE'; Start-Sleep -Seconds 4
    Save-Shot 'b0-mode'

    Send-Keys @('UP', 'UP', 'UP')                    # -> Alien: Isolation
    Send-Key 'SPACE'; Start-Sleep -Seconds 4
    Save-Shot 'b1-startmenu'

    Send-Keys @('UP', 'UP', 'UP')                    # -> Start Game
    Send-Key 'DOWN'; Start-Sleep -Seconds 2          # -> Continue Game
    Save-Shot 'b2-continue-selected'
    Send-Key 'SPACE'; Start-Sleep -Seconds 5
    Save-Shot 'b3-loading'

    # The mod logs this line the first frame a player camera exists. The overlay
    # listing it used to watch for is capped for the session and gets spent on
    # the menus, so it read as "never got in game" from a save that had loaded.
    $marker = 'player camera live'
    $deadline = (Get-Date).AddSeconds($LoadSeconds)
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path $log) -and (Select-String -Path $log -SimpleMatch $marker -Quiet)) { break }
        Start-Sleep -Seconds 3
    }
    Start-Sleep -Seconds 5
    Save-Shot 'd-ingame'
    if ((Test-Path $log) -and (Select-String -Path $log -SimpleMatch $marker -Quiet)) {
        Write-Host 'IN GAME'
    } else {
        # Back out of whatever menu we are stuck on. ESC only - never a confirm.
        foreach ($n in 1..3) { Send-Key 'ESC'; Start-Sleep -Seconds 2 }
        Save-Shot 'e-backed-out'
        Write-Host 'NOT IN GAME (backed out with ESC)'
    }
}

Save-Shot '00-start'
$i = 1
foreach ($k in $Keys) {
    Send-Key $k
    Start-Sleep -Seconds $KeyDelay
    Save-Shot ("{0:D2}-after-{1}" -f $i, $k)
    $i++
}
Write-Host "done"
