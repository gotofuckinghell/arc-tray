# Architecture

ArcTray is a single-process Win32 program. Two entry points share one core: the tray UI
(`no arguments`) and the CLI (anything starting with `--`). Both talk to the same four
modules, and the CLI path is the one every release is verified against.

```
                 ┌──────────────┐
   tray menu ───▶│  src/tray.c  │◀─── CLI (argv)
                 │ menu · INI   │
                 │ autostart    │
                 └──────┬───────┘
        ┌───────────────┼────────────────┬─────────────────┐
        ▼               ▼                ▼                 ▼
  ┌──────────┐   ┌────────────┐   ┌────────────┐   ┌──────────────┐
  │ src/arc.c│   │ src/gfx.c  │   │src/tweaks.c│   │  Win32 APIs  │
  │ IGCL +   │   │ 3D feature │   │  registry  │   │ tray, timer, │
  │ Sysman   │   │ get/set    │   │  on/off/   │   │ schtasks,    │
  │          │   │ snapshot   │   │  revert    │   │ Shell_Notify │
  └────┬─────┘   └─────┬──────┘   └────────────┘   └──────────────┘
       ▼               ▼
 IntelControlLib.dll   same DLL, ctlGetSupported3DCapabilities / ctlGetSet3DFeature
 ze_loader.dll (Sysman)
```

## Layers

### `src/arc.c` — hardware layer

- **Discovery.** `find_igcl_dll` walks the DriverStore (`*iigd*`, `*igfx*`, `*igdlh*`
  directories), then `System32`, then the Intel Graphics Software folder. The filename
  matters: `IntelControlLib.dll`, never the UID-whitelisted `ControlLib.dll`.
- **Two runtimes.** The driver-supplied DLL and, optionally, `IntelControlLib-2023.dll`
  next to the exe. The second one is only used where the current driver refuses a value
  (temperature limits above 90 °C). It is unsigned, so it is loaded only when its SHA-256
  matches `EXT_DLL_SHA256`.
- **Init.** `ctlInit` with `CTL_INIT_FLAG_USE_LEVEL_ZERO`, `Version = 0`,
  `AppVersion = 0x00010001`, and the ArcTool UID as a fallback. `ctlEnumerateDevices`
  finds the adapter; `ctlGetDeviceProperties` is walked V3 → V2 → V1 so older drivers
  answer with a smaller struct instead of `ERROR_INVALID_ARGUMENT`.
- **Sysman.** `zesInit` is called **without** `zeInit` — a mixed legacy Level Zero context
  makes `zesInit` fail. Power limits go through `zesPowerSetLimits` (sustained + burst),
  which is not clamped to the IGCL-advertised 276 W ceiling.
- **Apply + verify.** Every setter writes, sleeps ~150 ms, reads back, and compares
  against the requested value with an explicit tolerance (core offset matches within one
  driver grid step ±12 MHz, voltage within 6 mV). The message returned to the UI says
  which path was used (`(Sysman)`, `(IGCL)`, `(V1)`, `(ext)`).
- **Destructive-reset handling.** Setting the voltage offset back to 0 mV resets all IGCL
  overclock controls on the A770. `arc_set_volt(0)` therefore snapshots PL/core/temp,
  writes the zero, then restores the three values and reports each restore.

### `src/gfx.c` — non-overclocking 3D features

`ctlGetSupported3DCapabilities` is called twice: once for the count, once with a details
buffer. The count is clamped to the 64-slot buffer before the second call — a future stack
with more features must not write past it.

`ctlGetSet3DFeature` takes a `ctl_3d_feature_getset_t`. The value union differs per type:

| Value type | Layout inside the union |
|---|---|
| `BOOL` | `bool Enable` at offset 0 |
| `INT32` / `UINT32` / `FLOAT` | `bool Enable` at 0, value at 4 |
| `ENUM` | single `uint32 EnableType` at 0 — **no** separate enable flag |

That difference is the reason a naive write "succeeds" but reads back zero: the enum value
was written into the enable byte. `union_put` / `union_take` handle both shapes.

A feature that was never written answers `0x40000014 ERROR_DATA_NOT_FOUND`; after the
first write it reads back normally. The menu therefore only shows a submenu when the
feature is readable, and `--gfx` marks the rest `n/a`.

Ids used by ArcTray: `2` frame limit, `4` CMAA, `9` flip mode (bitmask: 1 app default,
2 vsync off, 4 vsync on, 8 smooth sync, 16 speed frame, 32 capped), `13` 64-bit atomics,
`16` low latency (0/1/2), `17` frame generation (0 app choice, 1 2×, 2 3×, 3 4×).
`11` app profiles, `15` scope (global vs per-app) and `19` live state are advertised but
answer `DATA_NOT_FOUND` or `CUSTOM` only, so they stay out of the UI.

### `src/tweaks.c` — registry

A table of nine tweaks: HKCU/HKLM path, value name, type (`REG_DWORD` or `REG_SZ`), the
"on" value, the "off"/revert value, an explanation and a reboot flag. `tweak_apply`
creates the key if needed, writes the value and reads it back; `tweak_revert` writes the
default. HKLM entries need an elevated process — the menu shows the failure instead of
silently skipping it.

### `src/tray.c` — shell

- **Menu.** Rebuilt on every right-click from live driver state, so labels always show
  the current value (`Power limit (325 W)`). All popups are destroyed after
  `TrackPopupMenu`.
- **Idle release.** The driver handle is opened when the menu is used and closed 20 s
  later by `IDLE_TIMER`; that is what keeps the idle footprint at 8.5 MB instead of 16 MB.
- **Tray icon.** `Shell_NotifyIcon` fails with `E_FAIL` when the logon task starts before
  Explorer creates its tray window, so registration is retried on a 500 ms timer (up to
  120 attempts) and re-done on the `TaskbarCreated` broadcast.
- **Single instance.** A `Local\ArcTray.Tray.SingleInstance` mutex; a second copy exits
  quietly, which matters because the logon task and a manual start would otherwise race.
- **Autostart.** `schtasks /Create /TN "ArcTray Elevated" /SC ONLOGON /RL HIGHEST /F`;
  the query result (not a registry flag) is the source of truth, cached for 20 s.
- **CLI.** Every flag is handled before any window is created, so the tool is scriptable
  and never flashes a window.

## Files and formats

| Path | Owner | Purpose |
|---|---|---|
| `<exe dir>\arctray.ini` | user | profiles and menu state; falls back to `%APPDATA%\ArcTray\` when read-only |
| `%APPDATA%\ArcTray\gfx-snapshot.ini` | `--gfx-save` | `name id enable value` lines, one per readable feature |
| `%APPDATA%\ArcTray\gfx-watch.log` | `--gfx-watch` | baseline block, then `timestamp name old/new` per change, hourly heartbeat, rotated at 1 MB |
| `<exe dir>\arctray.log` | tray | lifecycle log, truncated at 64 KB |

All three writers refuse `FILE_ATTRIBUTE_REPARSE_POINT` on the folder and on the file.

## Error model

IGCL results are 32-bit codes; `arc_result_name` maps the OC range (`0x4400000x`) and the
common `0x400000xx` codes to readable names, and unknown values are printed as hex. The
CLI exits `0` on success, `1` when the driver refused or the verification failed, `2` on
usage errors. The tray shows the same text in a balloon.

## Testing

There is no unit-test framework in the repository: the code talks to a kernel driver, so
the useful checks are live ones. Releases are verified with a throwaway script that

1. builds the product and the probes with `-Wall -Wextra` and a warning budget of zero
   for the files that changed,
2. round-trips every writable control and asserts the read-back,
3. snapshots graphics settings with `--gfx-save`, breaks two values, restores them and
   asserts the values match the snapshot (never "defaults" — that would wipe user state),
4. checks that the menu tree contains the expected entries.

Probes (`src/probe_*.c`) are read-only experiments kept as documentation of what the
driver reports on this card: structure sizes, supported feature ids, I2C enumeration,
Sysman overclock domains, peak power limits.
