# Changelog

All notable changes to this project are documented here. Format based on
[Keep a Changelog](https://keepachangelog.com/); this project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- The tracking mode (`Page Up`) and the yaw mode (`Page Down`) are saved to `CameraUnlock.ini` when you change them, so the next start uses them. `End` changes the current session only; `EnableOnStartup` decides whether tracking is on at startup.
- New settings: `UdpPort`, `EnableOnStartup`, the startup tracking mode (`RotationEnabled` and `PositionEnabled`) and the five lean limits (`PositionLimitX`, `PositionLimitY`, `PositionLimitYDown`, `PositionLimitZ`, `PositionLimitZBack`). Each defaults to the value the mod used before.
- Every hotkey is a key list you can rebind in `CameraUnlock.ini`: `ToggleKey`, `CycleTrackingModeKey` and `YawModeKey`, and the two diagnostic toggles `FrustumWideningKey` (`Insert, Ctrl+Shift+U`) and `InjectionModeKey` (`Delete, Ctrl+Shift+J`), which change the current session only.
- Added the C++ ASI plugin built on cameraunlock-core (CMake, x86, static CRT),
  with Ultimate ASI Loader vendored as the `xinput1_3.dll` proxy.
- Added the file logger, crash handler, PE build-fingerprint capture, OpenTrack
  UDP receiver, and nav-cluster hotkeys.
- Added a DX11 `Present`-hook overlay (MinHook plus the cameraunlock-core
  `DX11Overlay`), rendering on the game's backbuffer.

### Changed
- Settings move to `CameraUnlock.ini`. Earlier versions of the mod kept these settings in `AlienIsolationHeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `AlienIsolationHeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `AlienIsolationHeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `AlienIsolationHeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor is a `YawModeKey` set to Ctrl, Shift or Alt on its own: that key goes down before the key of any chord made with it, so the hotkey is left unbound, and it keeps its `Ctrl+Shift+H` chord. A `YawModeKey` code outside `0x01`-`0xFE` is left unbound the same way.
- An older version of the mod reads `AlienIsolationHeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `AlienIsolationHeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `AlienIsolationHeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- Against the `dev` pre-release, the import reads `SkipIntroMovies`, which `dev` did not have: `dev` always skipped the splash screens, and since 2517d2d they play unless `SkipIntroMovies=true`.
- Stripped the third-party DLLs Ultimate ASI Loader carries as resources out of
  the vendored copy. The upstream 32-bit build embeds `binkw32.dll` (RAD Game
  Tools' Bink and Smacker 1.994i, proprietary middleware licensed per title),
  `wndmode.dll` (DirectX Windower Embedded, (C) 2008 VEG and (C) 2004 menopem,
  no licence) and `vorbisfile.dll` (Xiph.Org, BSD-3-Clause) so that a user who
  renames the loader over one of those libraries still gets the original
  exports. The installer ZIP ships that binary, so it was redistributing all
  three. `scripts/strip-loader-payload.ps1` now zeroes them,
  `pixi run update-deps` runs it on every refresh, and `pixi run package`
  refuses to build a ZIP from a loader that still has them. Only the `.rsrc`
  section changes; every other byte of the file, and its size, are unchanged,
  and nothing in this mod could reach the stripped resources anyway.
- Corrected `THIRD-PARTY-NOTICES.md`: the reproduced cameraunlock-core licence
  named CameraUnlock as the copyright holder where the licence shipped with the
  pinned submodule names itsloopyo, and the recorded core commit had drifted
  from the one the submodule points at.
- Removed recentring from the mod: the `Home` hotkey, the `Ctrl+Shift+T` chord
  and the handler behind them are gone. Every tracker app centres itself, so a
  mod-side centre was a second centre in series with the tracker's and the two
  drifted apart. The mod now applies the tracker pose as absolute; centre it in
  your tracker app.
- Added the `LocalSmoothing` (default 0.0) and `RemoteSmoothing` (default 0.15)
  INI keys, selected per connection from the packet source address and covering
  both rotation and position. The mod previously had no smoothing key of its own
  and took whatever the core defaulted to.
- Removed the hidden 0.15 baseline smoothing floor, so a tracker on this machine
  gets zero-latency tracking by default.
- The log now names the matched build profile when the pinned hooks go in, not
  only when the build is unrecognised.
- The settings line is now written on a first launch too, where the INI is
  created rather than read.
- The log now keeps one previous generation as
  `AlienIsolationHeadTracking.prev.log`, so the crash report the handler writes
  survives the relaunch a player makes to go and read it. A rotation that fails
  (something else is holding the `.prev.log` open) is reported with its Windows
  error instead of silently losing the previous generation.

### Fixed
- The boot splash screens now play by default. The `skip_frontend` detour is
  behind a new `SkipIntroMovies` INI key, off unless the player turns it on:
  those screens carry the developer, publisher and rights-holder credits, and
  suppressing them is not something head tracking should do on its own.
- `THIRD-PARTY-NOTICES.md` names the cameraunlock-core commit the submodule
  actually points at, and credits 20th Century Studios alongside SEGA and
  Creative Assembly.
- The `camera: rotate -> ...` diagnostic is now latched per distinct reason. A
  player holding a neutral head pose flipped its reason every frame, which wrote
  megabytes an hour into the log; the running cap that replaced it was spent in
  under a second, so the reasons that only show up later ("faulted", "quaternion
  not unit") were never logged at all.
- The connection-locality line is only written once a packet has actually
  parsed. It used to be forced out on the first frame, where it read as proof a
  local tracker had connected when nothing had arrived.
