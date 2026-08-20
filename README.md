> [!CAUTION]
> ## Not exhaustively tested - expect rough edges
>
> Head tracking, aim decoupling and 6DOF position all work in game, but the mod
> has only been played through a small part of Alien: Isolation. Expect issues
> in situations it has not been put through yet.
>
> Please report anything you hit on the [issues page](https://github.com/itsloopyo/alien-isolation-headtracking/issues).

# Alien: Isolation Head Tracking

![Mod GIF](https://raw.githubusercontent.com/itsloopyo/alien-isolation-headtracking/main/assets/readme-clip.gif)

6DOF head tracking for Alien: Isolation that moves the in-game camera with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position
- **Aim-locked reticle and use prompts** - our reticle and the "E USE" prompt sit where the gun actually points

## Requirements

- Alien: Isolation on [Steam](https://store.steampowered.com/app/214490/) (App ID 214490), the 2015 retail build.
- An OpenTrack-compatible head tracker: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or VR headset, or a phone app that speaks the OpenTrack UDP protocol.
- Windows 10 or 11. The game is 32-bit, so the mod ships as a 32-bit `.asi`.

## Installation

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

1. Open OpenTrack.
2. Set Output to "UDP over network".
3. Set host to `127.0.0.1` and port to `4242`.
4. Map the axes you want (yaw, pitch, roll, x, y, z), center your head, and click Start.

### VR Headset Setup

1. Connect your headset to the PC with Air Link or Virtual Desktop and start SteamVR.
2. In OpenTrack, set Input to the SteamVR tracker.
3. Set Output to UDP on `127.0.0.1:4242` and click Start.

### Webcam Setup

1. In OpenTrack, set Input to the "neuralnet tracker".
2. Pick your webcam and let it find your face.
3. Set Output to UDP on `127.0.0.1:4242` and click Start.

### Phone App Setup

Phone trackers generally all speak OpenTrack UDP, but they differ in how much filtering they do on the phone, and that is what decides how you should wire them up:

- **Direct send**: point the app at your PC's LAN IP on port `4242`. This only works if the app filters its own signal on-device. A raw or lightly filtered feed sent straight to the mod will jitter, because the mod's smoothing is sized to take the edge off a clean signal rather than to rescue a noisy one. [Headcam](https://headcam.app) (my free tracking app) filters on-device, so can send directly.

  Not sure about yours? Try direct first. Hold your head still and watch the view: if it drifts or shakes, switch to the OpenTrack route below.
- **Via OpenTrack**: have the phone send to OpenTrack on a different port (for example 5252), then OpenTrack's Output forwards to `127.0.0.1:4242`. Apps that send a raw or lightly filtered signal need this route so OpenTrack's filters and curve mapping can clean the feed up first.

The mod's own smoothing and deadzone apply either way, but they are sized to take the edge off an already clean signal, not to rescue a noisy one.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action                    | Nav-cluster | Chord          |
|---------------------------|-------------|----------------|
| Toggle tracking           | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode       | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode           | `Page Down` | `Ctrl+Shift+H` |
| Toggle frustum widening   | `Insert`    | `Ctrl+Shift+U` |
| Cycle injection mode      | `Delete`    | `Ctrl+Shift+J` |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Page Down` / `Ctrl+Shift+H` switches yaw between horizon-locked (the default, head yaw turns about the world's up axis however the camera is pitched) and camera-local (head yaw turns about the camera's own up axis, which leans the view at steep pitches). The switch takes effect immediately and lasts for the session; the mod starts in whatever the INI says.

## Configuration

The config file is generated on first run next to `AI.exe` at `AlienIsolationHeadTracking.ini`. A setting the file does not carry keeps its default, so a file written by an older build still loads.

```ini
[General]
; Yaw mode: true = horizon-locked yaw (default), false = camera-local
WorldSpaceYaw=true
; Keep the space suit helmet on your head instead of leaving it facing where the body looks
HelmetFollowsHead=true
; Smoothing applied when the tracker runs on this machine (loopback). 0 = no smoothing, 1 = heavy
LocalSmoothing=0.0
; Smoothing applied when the tracker is a remote device on the network. 0 = no smoothing, 1 = heavy
RemoteSmoothing=0.15

[Hotkeys]
; Virtual-key code for the yaw-mode toggle.
YawModeKey=0x22    ; Page Down
```

`WorldSpaceYaw=true` (default) keeps yaw rotating around the world up-axis, so "up" stays gravity-aligned even when you look up or down. Set it to `false` for camera-local yaw, which follows the camera's current up-axis. Toggle it at runtime with `Page Down` or `Ctrl+Shift+H` without restarting.

`HelmetFollowsHead=true` (default) keeps the space suit helmet on your head as you look around, instead of it staying fixed to the body and leaving you looking out through the side of it.

`LocalSmoothing` and `RemoteSmoothing` are picked per connection from the address the packets arrive from, and both cover rotation and position. A tracker on this PC (OpenTrack over loopback) uses `LocalSmoothing`, which defaults to `0.0` for zero-latency tracking; a phone or other device on the network uses `RemoteSmoothing`, which defaults to `0.15` because network jitter needs it. Switching between the two is picked up without restarting the game.

## Troubleshooting

**Mod not loading**
- Confirm `xinput1_3.dll` and `AlienIsolationHeadTracking.asi` are both next to `AI.exe`.
- Launch through Steam, not by running `AI.exe` directly.
- Look for `AlienIsolationHeadTracking.log` next to `AI.exe`. If it is missing, the loader did not pick up the `.asi`.

**No tracking response**
- Make sure OpenTrack is running and Started, with output set to UDP on `127.0.0.1:4242`.
- Check that port `4242` is not blocked by your firewall.
- Open `AlienIsolationHeadTracking.log` and read the lines about the UDP connection and incoming poses. The log starts fresh every launch; the previous run is kept as `AlienIsolationHeadTracking.prev.log`, which is the one to send after a crash.
- If another tracker or modded game already holds port `4242`, close it. The mod retries automatically.

**Jittery or unstable tracking**
- Raise `LocalSmoothing` (tracker on this PC) or `RemoteSmoothing` (phone or other network device) toward `1.0` in `AlienIsolationHeadTracking.ini`.
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
- Set `HelmetFollowsHead=true` under `[General]` in `AlienIsolationHeadTracking.ini` (it is on by default).

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

This mod is not affiliated with, endorsed by, or supported by Creative Assembly or SEGA. Use at your own risk.
