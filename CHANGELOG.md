# Changelog

## 0.0.1

Versioning restart: the project is pre-1.0 and stays at `0.0.N` until the menu/CLI/config
surface is frozen — the `1.x` numbering implied a stability that does not exist yet. The
`1.x` tags stay in git history; this release re-tags the same tree (the data-loss fix for
bit-step tweaks included) as `0.0.1`.

All notable changes are recorded here. The project follows
[Semantic Versioning 2.0.0](https://semver.org/):

- **MAJOR** — breaks how the tool is used (menu layout, CLI flags, config format) or
  drops a supported driver generation.
- **MINOR** — backwards-compatible functionality (a new menu block, CLI command, tweak).
- **PATCH** — fixes and documentation only.

Every release is tagged `vMAJOR.MINOR.PATCH`; `src/version.h` is the single source of
truth for the number the binary reports.

## 1.9.1

- **Data-loss fix in the bit-step write path.** `write_step` opened the key with
  `KEY_SET_VALUE` only, so the read-modify-write for a bit step (`dpst`) could not read the
  current `FeatureTestControl` and wrote `0` instead of preserving the other bits. The key
  is now opened with `KEY_SET_VALUE | KEY_QUERY_VALUE`, verified by re-reading
  `FeatureTestControl = 0x1200` around a `dpst` revert.

## 1.9.0

- **Five new registry tweaks from the driver research**: `async-flips`
  (`Display1_DisableAsyncFlips` in the adapter class key), `overlay-ds`
  (`GraphicsDrivers\Disable_OverlayDSQualityEnhancement`), `hw-overlay`
  (`DirectX\DisableHWOverlay`), `sched-queue`
  (`GraphicsDrivers\Scheduler\HwQueuedRenderPacketGroupLimit`, 2 instead of 0; the
  community note is that 1 *decreases* FPS) and `dpst` (bit 5 of `FeatureTestControl`,
  a no-op on desktop cards but the documented DPST switch).
- Tweak engine: `%ADAPTER%` in a path is resolved at runtime by scanning
  `Class\{4d36e968-…}\0000…` for the Intel `DriverDesc`, so a machine with two GPUs still
  hits the Arc entry; steps can now **set/clear a bit** instead of writing a whole DWORD;
  and a step can mark `revert_write` so revert restores the driver's own value instead of
  deleting it (deleting `Display1_DisableAsyncFlips`/`FeatureTestControl` would destroy
  values the driver created).
- Menu and `--tweaks` now list 14 tweaks with the same one-line explanations.

## 1.8.2

Static audit (OpenCode `--agent plan` over `tray.c`/`arc.c`/`gfx.c`/`tweaks.c`, plus manual
triage of every claim). Confirmed and fixed:

- `arc_set_core` reported `ok` when the read-back failed (`NaN`): the comparison was false
  and the message printed `nan`. It now fails with `readback unavailable`.
- `arc_open` leaked the IGCL client and the loaded DLL on every failure after `ctlInit`
  (no devices, no Intel adapter). One cleanup helper now runs on those paths.
- `arc_set_power` clamps to 1…600 W before `lround(watts * 1000)`; `arc_set_volt` clamps
  the requested offset to 0…500 mV before the branch that writes it.
- `arc_ext_available` cached "extended runtime unavailable" *before* the probe, so one
  transient failure disabled the >90 °C route for the whole process. The flag is set only
  after a successful probe.
- `ctlFanSetSpeedTableMode` was bound as `ctl_fan_speed_t *` and called through a cast;
  the prototype now matches IGCL (`ctl_fan_speed_table_t *`).
- `gfx`: reading a BOOL feature produced an uninitialised `value`, and a FLOAT feature
  would have been read as an integer; the union accessor now branches per value type. The
  empty application name gets a 2-byte buffer so `Length = 1` always has a NUL.
- `--profile=` skipped the voltage offset and always used the ≤90 °C setter, so a profile
  with `temp=110` silently applied 90. It now mirrors the menu path (ext route + volts).
- `GetTickCount` wraparound in the autostart cache (`(DWORD)` subtraction).

Rejected after checking the code (reported as findings, not real): `KEY_WOW64_64KEY`
(the build is x64, no Wow6432Node), "INT features leave the value uninitialised" (the
INT layout does carry `Value`), and the hard-coded `stype = 0x1c` — unverified against the
Level Zero headers, left as is.

## 1.8.1

- Documentation: rewritten `README.md`, new `docs/ARCHITECTURE.md` (layers, init sequence,
  3D-feature union layouts, files, testing) and `docs/CLI.md` (every flag, ranges, exit
  codes, environment switches).
- Tray is single-instance (`Local\ArcTray.Tray.SingleInstance`); a second copy exits
  quietly instead of duplicating the icon and racing the logon task.
- Repository hygiene: tracked probe binaries removed, `.gitignore` covers build output.

## 1.8.0

Security audit of the whole tool (see `SECURITY.md`, "Audit 2026-10-29"):

- **SHA-256 pin for the unsigned 2023 IGCL runtime** — the bundled DLL has no Authenticode
  signature, so it is loaded only when its hash matches `EXT_DLL_SHA256`
  (`B1D0FAD7…297EB`); `ARCTRAY_ALLOW_UNSIGNED=1` is the documented escape hatch.
- Both runtimes load through `LoadLibraryExA(LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
  LOAD_LIBRARY_SEARCH_SYSTEM32)` — no current-directory/PATH import resolution.
- `notepad.exe` (config editor) runs by absolute `%SystemRoot%\System32` path.
- Profile values clamped to the advertised ranges (PL 115–500 W, core 0–300 MHz,
  voltage 0–288 mV, temp 60–115 °C, fan 30–100 %), each clamp logged.
- Snapshot, restore and watch refuse reparse points at file level as well as folder level.
- `--state` fan readout fixed: RPM was printed with a `%` suffix.

## 1.7.1

Code-review pass over `gfx.c`, `arc.c`, `tray.c` (gcc `-fanalyzer` + manual read):

- `gfx_caps`: the details array is 64 slots but the count came straight from the
  driver — a stack advertising more features would have had it write past the buffer.
  The request is now clamped to 64 and a driver that refuses the smaller request is
  treated as "no features" instead of reading uninitialised memory.
- Snapshot and watcher paths: the `%APPDATA%\ArcTray` folder is created when missing
  (saving used to fail silently on a fresh profile) and both refuse to write through a
  junction/reparse point.
- `--gfx-watch` rotates the log at 1 MB (`gfx-watch.log.1`), so a multi-day run cannot
  grow without bound.
- `ini_path`: bounds check before appending `\arctray.ini` (size_t underflow when the
  directory path filled the buffer); profile menu labels widened to 192 chars;
  `find_dll` skips candidates longer than `MAX_PATH`; unused `g_core_mode` removed.

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
