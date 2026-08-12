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
