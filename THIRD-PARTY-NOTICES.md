# Third-Party Notices

Alien: Isolation Head Tracking itself is MIT licensed (see `LICENSE`). It bundles
or links the following components, which carry their own terms.

## Ultimate ASI Loader

- **Version:** v9.7.2 (commit `ab722befd52581a34449b603926cfab476e66b05`)
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** Renamed to `xinput1_3.dll` and dropped beside `AI.exe` so the game
  loads our `.asi` plugin.
- **Bundled:** yes. Bundled in the release ZIP and used as the install-time
  source.

Copyright (c) 2023 ThirteenAG

---

## MinHook

- **Version:** `master` (fetched and pinned at build time via CMake FetchContent)
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Installs the DX11 `Present` hook and the camera-update hook.
- **Bundled:** yes. Statically linked into `AlienIsolationHeadTracking.asi`.

Copyright (c) 2009-2017 Tsuda Kageyu

---

## cameraunlock-core

- **Version:** commit `3465659`
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared CameraUnlock protocol, processing, hook, and rendering
  library.
- **Bundled:** yes. Statically linked into `AlienIsolationHeadTracking.asi`.

Copyright (c) 2026 CameraUnlock

---

## OpenTrack

- **Version:** protocol only, no code used
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** We consume the OpenTrack UDP pose protocol; no OpenTrack code is
  compiled in or shipped.
- **Bundled:** no.

---

## Game

Alien: Isolation was developed by Creative Assembly and published by SEGA. The
Alien franchise and its related marks are the property of 20th Century Studios.
This mod is an unofficial, fan-made add-on and is not affiliated with or endorsed
by any of them. Those names appear here only to identify the game this mod is
compatible with.

No game code, assets, media, or binaries are included in this repository or in
its releases. The mod hooks the game in memory at runtime and requires a
legitimately purchased copy to be of any use.
