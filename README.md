# Alien: Isolation Head Tracking

![Alien: Isolation running with this mod](https://raw.githubusercontent.com/itsloopyo/alien-isolation-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Alien: Isolation that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- Alien: Isolation on [Steam](https://store.steampowered.com/app/214490/) (App ID 214490), the 2015 retail build.
- An OpenTrack-compatible head tracker: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or VR headset, or a phone app that speaks the OpenTrack UDP protocol.
- Windows 10 or 11. The game is 32-bit, so the mod ships as a 32-bit `.asi`.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Alien: Isolation**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the installer ZIP from the [Releases page](https://github.com/itsloopyo/alien-isolation-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. In OpenTrack, set the output to UDP and send to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, point it at the install folder with an environment variable:

```powershell
set ALIEN_ISOLATION_PATH=D:\Games\Alien Isolation
install.cmd
```

or pass the folder as the first argument:

```powershell
install.cmd "D:\Games\Alien Isolation"
```

### Manual Installation

The installer places two files next to `AI.exe` in the game folder. To do it by hand:

1. Copy the Ultimate ASI Loader from `vendor/ultimate-asi-loader/dinput8.dll` into the game folder and rename it to `xinput1_3.dll`. If an `xinput1_3.dll` from another mod is already there, leave it in place; the loader only needs to exist once.
2. Copy `AlienIsolationHeadTracking.asi` from the `plugins` folder into the same directory.

The full path is usually:

```
<SteamLibrary>/steamapps/common/Alien Isolation/
```

The Nexus release ZIP contains only `AlienIsolationHeadTracking.asi`, for users who already run an ASI loader.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action                    | Nav-cluster | Chord          |
|---------------------------|-------------|----------------|
| Toggle tracking           | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode       | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode           | `Page Down` | `Ctrl+Shift+H` |
| Toggle frustum widening   | `Insert`    | `Ctrl+Shift+U` |
| Cycle injection mode      | `Delete`    | `Ctrl+Shift+J` |

Each action's keys are a list under `[Hotkeys]` in `CameraUnlock.ini`, so you can rebind or remove any of them.

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

The mode you pick is saved to `CameraUnlock.ini` and used at the next start. `End` changes the current session only: at startup tracking is on or off as `EnableOnStartup` says.

`Page Down` / `Ctrl+Shift+H` switches yaw between horizon-locked (the default, head yaw turns about the world's up axis however the camera is pitched) and camera-local (head yaw turns about the camera's own up axis, which leans the view at steep pitches). The switch takes effect immediately and is saved to `CameraUnlock.ini`, so the next start uses it too.

## Configuration

Apart from creating `CameraUnlock.ini` at startup when there is none, the mod writes to it only when a hotkey changes the tracking mode or the yaw mode. It never writes `AlienIsolationHeadTracking.ini`, and it creates `Defaults.ini` only when there is none and never changes it. Edit `CameraUnlock.ini` with the game closed.

<!-- cameraunlock:config -->
The mod reads its settings from `CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; Alien: Isolation head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default
; true: the space suit helmet stays on your head as you look around.
; false: it stays facing where your body looks.
HelmetFollowsHead=true
; true: skip the splash screens at startup. They carry the developer and
; publisher credits, so this is off unless you turn it on.
SkipIntroMovies=false

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default
; Diagnostic: widens the view the game culls against, for this session only.
; The picture zooms out while it is on.
FrustumWideningKey=Insert, Ctrl+Shift+U
; Diagnostic: cycles where the head pose is applied, for this session only.
InjectionModeKey=Delete, Ctrl+Shift+J
```
<!-- /cameraunlock:config -->

There are no sensitivity, inversion or deadzone settings: the mod applies the pose your tracker sends, so set those in the tracker.

`HelmetFollowsHead=true` keeps the space suit helmet on your head as you look around, instead of it staying fixed to the body and leaving you looking out through the side of it.

`SkipIntroMovies=false` leaves the game's boot sequence exactly as
Creative Assembly shipped it. Set it to `true` and the mod answers yes to the
game's own `skip_frontend` flag, which drops you at the title screen without the
company splash screens. It is off by default because those screens are where the
game's credits are shown; skipping them is your call, not something head
tracking should do behind your back.

`LocalSmoothing` is used for a tracker on this PC (OpenTrack over loopback) and `RemoteSmoothing` for a phone or other device on the network, picked from the address the packets arrive from. Both cover rotation and position, and switching between the two trackers is picked up without restarting the game.

`FrustumWideningKey` and `InjectionModeKey` are diagnostic toggles. They change the current session only and are never saved.

## Troubleshooting

**Mod not loading**
- Confirm `xinput1_3.dll` and `AlienIsolationHeadTracking.asi` are both next to `AI.exe`.
- Launch through Steam, not by running `AI.exe` directly.
- Look for `HeadTracking.log` next to `AI.exe`. If it is missing, the loader did not pick up the `.asi`.

**No tracking response**
- Make sure OpenTrack is running and Started, with output set to UDP on `127.0.0.1:4242`.
- Check that port `4242` is not blocked by your firewall.
- Open `HeadTracking.log` and read the lines about the UDP connection and the pose sample it writes about once a minute of play. The log starts fresh every launch; the previous run is kept as `HeadTracking.prev.log`, which is the one to send after a crash.
- If another tracker or modded game already holds port `4242`, close it. The mod retries automatically.

**Jittery or unstable tracking**
- Raise `LocalSmoothing` (tracker on this PC) or `RemoteSmoothing` (phone or other network device) toward `1.0` in `CameraUnlock.ini`.
- Raise the smoothing in OpenTrack (or in your phone app, if it sends directly).
- If a phone app sends straight to `4242` and the view will not settle, route it through OpenTrack instead (see [Phone App Setup](#phone-app-setup)) and select a filter, e.g. Accela, to clean up the signal.
- Add a small deadzone in OpenTrack's mapping curves to ignore tiny head movements.
- For wireless or phone trackers, prefer a wired or 5 GHz connection; dropped packets read as jitter.

**Wrong rotation axis**
- Invert the offending axis in OpenTrack's mapping (each axis has an invert checkbox).
- Confirm the axis is actually mapped in OpenTrack; an unmapped axis reads as a dead one.

**Yaw feels wrong when looking up or down at extreme angles**
- Toggle between world-locked and camera-local yaw with `Page Down` or `Ctrl+Shift+H`. World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

**The space suit helmet does not move with my head**
- Set `HelmetFollowsHead=true` under `[General]` in `CameraUnlock.ini` (it is on by default).

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod's `.asi`. The Ultimate ASI Loader (`xinput1_3.dll`) is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

```bash
git clone --recurse-submodules https://github.com/itsloopyo/alien-isolation-headtracking
cd alien-isolation-headtracking
pixi run build
pixi run deploy
```

Requires Visual Studio 2022 (with the C++ workload) and CMake; MinHook is fetched by CMake. Output lands at `build/Release/AlienIsolationHeadTracking.asi`.

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details. Third-party components are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- Alien: Isolation developed by Creative Assembly, published by SEGA.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG (MIT).
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause).
- [OpenTrack](https://github.com/opentrack/opentrack) (ISC).
- Built on the shared [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) framework.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Creative Assembly,
SEGA, or 20th Century Studios. Alien: Isolation, the Alien franchise, and all
related names and marks are the property of their respective owners, and are used
here only to identify the game this mod is compatible with. No game code, no
extracted assets and no data files are included in this repository or in its
releases, and the mod requires your own legitimately purchased copy of the game.
The short gameplay clip at the top of this page is captured footage of the game,
copyright Creative Assembly and SEGA, shown so a reader can see what the mod
does. It stays in the repository and ships in neither release ZIP. Happy to take
it down on request. Use at your own risk.
