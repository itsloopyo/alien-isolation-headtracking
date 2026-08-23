# Changelog

All notable changes to this project are documented here. Format based on
[Keep a Changelog](https://keepachangelog.com/); this project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Added the C++ ASI plugin built on cameraunlock-core (CMake, x86, static CRT),
  with Ultimate ASI Loader vendored as the `xinput1_3.dll` proxy.
- Added the file logger, crash handler, PE build-fingerprint capture, OpenTrack
  UDP receiver, and nav-cluster hotkeys.
- Added a DX11 `Present`-hook overlay (MinHook plus the cameraunlock-core
  `DX11Overlay`), rendering on the game's backbuffer.

### Changed
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
