# Changelog

All notable changes are recorded here. The project follows
[Semantic Versioning 2.0.0](https://semver.org/):

- **MAJOR** — breaks how the tool is used (menu layout, CLI flags, config format) or
  drops a supported driver generation.
- **MINOR** — backwards-compatible functionality (a new menu block, CLI command, tweak).
- **PATCH** — fixes and documentation only.

Every release is tagged `vMAJOR.MINOR.PATCH`; `src/version.h` is the single source of
truth for the number the binary reports.

## 1.7.0

- `--gfx-watch[=seconds]`: polls every supported 3D feature and appends a timestamped
  `name  old_enable/old_value -> new_enable/new_value` line to
  `%APPDATA%\ArcTray\gfx-watch.log` whenever something changes, plus a baseline block
  at start, `became readable` / `now unreadable` transitions and an hourly heartbeat.
  Default interval 30 s (minimum 5). Used to catch a third party (IGS, driver) quietly
  rewriting the user's graphics settings.

## 1.6.0

- `--gfx-save[=file]` / `--gfx-restore[=file]` and the matching **Graphics → Save these
  settings / Restore saved settings** entries. The snapshot keeps every readable feature
  as `name id enable value` in `%APPDATA%\ArcTray\gfx-snapshot.ini`, and restore writes
  each one back and verifies the read-back before reporting success.
- Verified round-trip: 6 features saved, one deliberately broken (low latency 2→0),
  restore returned all 6 with `read back ok`, problems 0.

## 1.5.1

- Every Graphics entry now carries a one-line explanation in its label
  (`vsync off — tearing, lowest lag`, `on + boost — lowest lag, more power`, …), so the
  menu answers "what does this do" without a manual.
- Build warning cleanup (`unused variable` in the Graphics block).

## 1.5.0

- Graphics menu: **Frame generation (XeSS FG)** (app choice / 2x / 3x / 4x), **CMAA**
  and **emulated 64-bit atomics** added next to the existing low-latency, VSync/flip
  and FPS-limit entries. All of them write through `ctlGetSet3DFeature` and are read
  back before the tick is drawn.

## 1.4.0

- **Graphics** menu block and `--gfx` / `--gfx-set=<name>:<value>`: FPS limit,
  VSync/flip mode (bitmask), low latency and XeSS frame-generation override through
  `ctlGetSet3DFeature`, with read-back verification and correct union handling per
  value type (ENUM is a uint32 at the union start, INT/UINT are {bool,value}).
- `src/gfx.c`/`gfx.h`, `src/probe_i2c.c`, `src/probe_gfx.c` (research probes).
- docs: I2C findings (enumeration works — 5 pin pairs, 25 display outputs; every
  transaction answers `0x40000017 ERROR_KMD_CALL`, so the KMD gate is still closed).

## 1.3.0

- **Tweaks (latency)** menu block and `--tweaks` / `--tweak=<id>:on|off|revert`:
  HAGS, MPO workaround, Game DVR, fullscreen optimizations, MMCSS Games priority,
  MMCSS system responsiveness, network throttling, global timer resolution,
  Win32PrioritySeparation. Each entry has a one-line description in the menu, a full
  text in the balloon and the help dialog, and every write is read back.
- About became a submenu: About dialog plus GitHub / GitLab / Telegram links.
- `src/version.h`: single source of truth for the SemVer version.

## 1.2.1

- Tray icon fix: `Shell_NotifyIcon` is retried when Explorer is not ready (the logon
  task used to lose the icon with `E_FAIL`), the window is a hidden top-level popup so
  the `TaskbarCreated` broadcast arrives, and the icon is re-registered after an
  Explorer restart.

## 1.2.0

- `--watch[=sec]`: real board power in watts from the IGCL energy counter (which
  turns out to be a double in joules, not a u64 counter). Used to verify power limits
  without external tools.
- docs: measured idle 39.5 W and 277…282 W under a llama.cpp SYCL load with the
  `POWER` + `CURRENT` limit flags; FurMark/Superposition are not valid power tests on
  this card, and ~280 W is the current-limited ceiling.

## 1.1.0

- Tray menu with profiles, power limit, core offset, voltage offset, temperature
  limit and fan control; every apply is verified by read-back.
- Fan control on the A770 through the flat 2-point speed table (the fixed-speed API
  returns `ERROR_UNSUPPORTED_FEATURE`).
- Temperature limit up to 115 °C through the optional 2023 IGCL runtime.
- Autostart as an elevated logon task, toggled from the tray.
- About / Links entries (attribution and project links).
- Portable layout: config next to the exe, DLLs next to the exe, no installation.
- Driver-branch robustness: IGCL discovery fallbacks, structure-version walk,
  V1 setters, IGCL power-limit fallback, V1 telemetry.
- Security: environment DLL overrides ignored when elevated, log moved out of
  `%TEMP%` and protected against reparse points, out-of-bounds fix in the runtime
  path list (see SECURITY.md).

## 1.0.0

- First working build: power limit through Level Zero Sysman, core offset and
  temperature limit through IGCL, tray icon with preset submenus, CLI mode.
