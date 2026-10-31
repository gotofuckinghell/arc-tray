# ArcTray

Headless tuner and graphics control panel for **Intel Arc** GPUs: one native Win32
binary, a tray menu and a full CLI. No Electron, no window, no service, no network, no
telemetry.

ArcTray is the tuner half of [Arc-Power](https://github.com/YamsSE/Arc-Power) rebuilt in
C: the same IGCL and Level Zero Sysman calls, without the renderer, overlay, recording
pipeline and ~150 MB Electron runtime. What is left is a ~91 KB executable that costs
**8.5–16 MB** of working set and applies the same controls as the vendor tool.

```
right-click the tray icon
├── Arc A770 — 325 W · 2400 MHz · 51 °C
├── Profiles ▸ Gaming · Quiet · Stock
├── Power limit (325 W) ▸ 115 … 420 W
├── Core offset (+0 MHz) ▸ 0 … +300 MHz
├── Temperature limit (90 °C) ▸ 60 … 115 °C
├── Voltage offset (0 mV) ▸ 0 … +288 mV
├── Fan ▸ Auto · 30 … 100 %
├── Graphics ▸ Low latency · VSync/flip · FPS limit · XeSS FG · CMAA · atomics
│              Save these settings · Restore saved settings
├── Registry tweaks ▸ HAGS · MPO · GameDVR · FSO · MMCSS · timers · throttling
├── Start with Windows
├── Links ▸ GitHub · GitLab · Telegram
└── About ArcTray… · Exit
```

## What it does

### Tuner (IGCL + Level Zero Sysman)

| Control | Range | How |
|---|---|---|
| Power limit | 115…420 W | Level Zero Sysman `zesPowerSetLimits` (sustained + burst), verified by read-back; IGCL fallback clamps to the advertised range |
| Core clock offset | 0…+300 MHz | `ctlOverclockGpuFrequencyOffsetSetV2`, V1 fallback |
| Temperature limit | 60…115 °C | 60…90 through the driver runtime, 91…115 through the bundled 2023 IGCL runtime |
| Voltage offset | 0…+288 mV | `ctlOverclockGpuMaxVoltageOffsetSetV2` (positive only) |
| Fan | auto / 30…100 % | flat 2-point table via `ctlFanSetSpeedTableMode` (the A770 rejects the fixed-speed API) |
| Profiles | — | `arctray.ini` next to the exe (or `%APPDATA%\ArcTray`) |

Every write is read back and the driver's answer is shown in a balloon instead of a fake
"OK". Failed applies report the exact IGCL / Level Zero result code. Clearing the voltage
offset to 0 mV resets *all* IGCL overclock controls on this card, so ArcTray snapshots and
restores power limit, core and temperature around it.

### Graphics (non-overclocking 3D features)

| Feature | Values |
|---|---|
| FPS limit | off · 30 · 60 · 90 · 120 · 144 · 165 · 240 |
| VSync / flip mode | application default · vsync off · vsync on · smooth sync · speed frame · capped to panel rate |
| Low latency | off · on · on + boost |
| Frame generation (XeSS FG) | application choice · 2x · 3x · 4x |
| CMAA | off · on |
| Emulated 64-bit atomics | off · on |

Each entry carries a one-line explanation in its label, values are verified by read-back,
and `--gfx-save` / `--gfx-restore` keep a named snapshot. `--gfx-watch` logs every change
with a timestamp, so a third-party tool that quietly rewrites these settings is visible.

### Registry tweaks

Fourteen reversible settings with a one-line description each: hardware-accelerated GPU
scheduling, MPO workaround, Game DVR, fullscreen optimisations, MMCSS "Games" priority,
MMCSS system responsiveness, network throttling, global timer resolution,
`Win32PrioritySeparation`, plus the display/scheduler findings — **disable async flips**
(`Display1_DisableAsyncFlips`, adapter class key), **overlay quality enhancement off**,
**DirectX hardware overlay off**, **GPU scheduler queue limit 2**
(`HwQueuedRenderPacketGroupLimit`) and **panel power saving (DPST) off** (bit 5 of
`FeatureTestControl`). `--tweak=<id>:on|off|revert`, or the menu.

## Requirements

- Windows 10 1607+ or Windows 11 / Server 2025, **x64**
- Intel Arc GPU with a driver that ships IGCL (`IntelControlLib.dll`) and, for the power
  limit, Level Zero Sysman (`ze_loader.dll`)
- Administrator rights for writes (a non-elevated IGCL overclock write returns
  `ERROR_OC_INSUFFICIENT_PERMISSIONS` or succeeds and then reverts)
- No redistributables: the exe is statically linked

Not affiliated with Intel. Overclocking can crash or hang the GPU; a hang is recovered by
a reboot (TDR).

## Install (portable)

```
ArcTray\
  arctray.exe               the tool
  IntelControlLib-2023.dll  optional: temperature limits above 90 °C
  arctray.ini               created on first run (profiles + settings)
  arctray.log               created on first run
```

1. Put the folder somewhere only administrators can write, e.g. `C:\Program Files\ArcTray`.
2. Optional: `arc.ico` next to the exe for the tray icon.
3. Optional: `IntelControlLib-2023.dll` — Intel's 2023 IGCL runtime, needed only for
   temperature limits above 90 °C. ArcTray refuses to load it unless its SHA-256 matches
   the pinned value (see *Security*); this repository never ships Intel binaries.
4. Run it elevated once and tick **Start with Windows**. The app creates the logon task
   `ArcTray Elevated` (`/SC ONLOGON /RL HIGHEST`, InteractiveToken), so the tray starts
   with rights and without a UAC prompt.

`arctray.ini` is created next to the exe; if that folder is read-only the app falls back to
`%APPDATA%\ArcTray\arctray.ini`.

> **Install location matters.** An elevated tray started from a user-writable folder is a
> local privilege-escalation vector: whoever can write `arctray.exe` gets code execution as
> administrator at logon. Use an administrators-only folder, or do not enable the logon
> task. See [SECURITY.md](SECURITY.md).

## CLI

```
arctray.exe                             tray icon (no window)
arctray.exe --state                     caps + live values
arctray.exe --set pl=398 core=75 temp=95 fan=60
arctray.exe --set fan=auto
arctray.exe --set volt=50               voltage offset, mV (positive only)
arctray.exe --profile=Gaming            apply an ini profile
arctray.exe --watch[=sec]               board power (W) from the IGCL energy counter
arctray.exe --gfx                       list 3D features with current values
arctray.exe --gfx-set=low-latency:2     write one feature, read it back
arctray.exe --gfx-save[=file]           snapshot all readable features
arctray.exe --gfx-restore[=file]        write them back and verify
arctray.exe --gfx-watch[=sec]           log settings changes with timestamps
arctray.exe --tweaks                    latency registry tweaks with their state
arctray.exe --tweak=<id>:on|off|revert  toggle one tweak
arctray.exe --autostart=on|off|status   logon task
arctray.exe --selftest                  OS build, driver, which IGCL/Sysman symbols exist
arctray.exe --caps                      advertised ranges and current targets
arctray.exe --about                     version, attribution, links
arctray.exe --menudump                  print the menu tree (labels + ids)
arctray.exe --menu                      ask the running tray to open its menu (at the cursor)
arctray.exe --quit                      ask the running tray to exit
```

Full reference, exit codes and examples: [docs/CLI.md](docs/CLI.md).

## Config

```ini
; profile=Name,PowerW,CoreOffsetMHz,VoltOffsetmV,TempC,Fan(auto|30..100)
profile=Gaming,375,0,0,90,auto
profile=Quiet,150,0,0,85,auto
profile=Stock,230,0,0,90,auto
```

Hand-edited values are clamped to the advertised ranges and every clamp is logged.

Graphics snapshot (`--gfx-save`, default `%APPDATA%\ArcTray\gfx-snapshot.ini`):

```
# name id enable value
frame-limit 2 1 240
cmaa 4 0 0
flip-mode 9 1 2
atomics-64 13 1 1
low-latency 16 1 2
frame-gen 17 0 0
```

## Build

Requires only an x64 mingw-w64 gcc (Strawberry Perl ships one as
`x86_64-w64-mingw32-gcc`). The icon is a tracked file (`res/arc.ico`), so no Python is needed
any more.

```bash
bash build.sh             # -> arctray.exe + arc.ico
bash scripts/verify.sh    # build + invariants (read-only); --live also reads the card
```

| File | Role |
|---|---|
| `src/tray.c` | Win32 tray, menu, INI, autostart task, CLI |
| `src/arc.c` / `arc.h` | DLL discovery, IGCL + Sysman init, state read, apply paths |
| `src/gfx.c` / `gfx.h` | 3D features, snapshot, watcher |
| `src/tweaks.c` / `tweaks.h` | registry tweaks |
| `src/probe*.c` | read-only probes used to pin structure versions on a live card; not part of the binary |

Architecture and driver details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
Driver quirks that shaped the code: [docs/A770-NOTES.md](docs/A770-NOTES.md).

## Versioning

**Pre-1.0, `0.0.<base><quarters>`**: the menu, CLI and config format are still moving, so
there is no stable release. The counter works as requested:

* `base` — three digits, **+1 for every added option**;
* `quarters` — two digits appended only when non-zero: `25` / `50` / `75`,
  because **one fixed bug adds 0.25** (four bugs = one feature step).

So `0.0.004` = four features, `0.0.00425` = plus one bug fix. The number lives in
`src/version.h`, the binary prints it (`--about`), releases are tagged `v0.0.<base><quarters>`,
and every tag has a matching [CHANGELOG](CHANGELOG.md) entry. `1.0.0` is reserved for a
frozen interface; the old `1.x` tags stay in history.

## Robustness across driver branches

- **IGCL discovery** — `ARCTRAY_IGCL` (non-elevated only) → DriverStore
  `*iigd*`/`*igfx*`/`*igdlh*` → `System32\IntelControlLib.dll` → Intel Graphics Software
  folder. `System32\ControlLib.dll` is never used (UID-whitelisted loader).
- **Structure versions** — `ctlGetDeviceProperties` walks Version 3 → 2 → 1 and
  `ctlOverclockGetProperties` walks 1 → 0.
- **Setters** — every V2 call falls back to the V1 symbol; the message says `(V1)` when
  that path was used.
- **Power limit** — without Level Zero Sysman the write goes through IGCL, clamped to the
  advertised range (A770: 115…276 W), and says `ok (IGCL)`.
- **Telemetry** — `ctlPowerTelemetryGetV2` falls back to the V1 1024-byte structure.

Debug switches: `ARCTRAY_NO_SYSMAN=1`, `ARCTRAY_V1=1`, `ARCTRAY_PROPS_VERSION=1`.
DLL-path overrides are ignored in an elevated process by design (see SECURITY.md).

## Measuring a power limit

`arctray.exe --watch=20` prints board power twice a second, derived from the IGCL energy
counter (a **double in joules**; `gpuPowerPercent` is unsupported on this stack). Measured
on the A770 with a 398 W limit:

```
idle                                       39.5 W
llama-bench (SYCL, MiniCPM5-2B, -p 4096)   277…282 W, 2532 MHz, 71 °C, flags POWER CURRENT
```

A real compute load exceeds the 160–180 W that FurMark and Superposition reach — those two
do not saturate Xe. The card stops at ~280 W with the **current** limit flag set, so the
VRM/connector budget binds before the 398 W register value matters (2×8-pin + slot is
375 W on paper). Treat >300 W as unreachable on this board rather than as a target.

## Security

- No network code, no IPC, no service (`WSAStartup|socket|WinHttp|InternetOpen|URLDownload`
  → zero hits), no secrets in the repository.
- The unsigned 2023 IGCL runtime loads only when its SHA-256 matches the pinned value;
  `ARCTRAY_ALLOW_UNSIGNED=1` is the deliberate override.
- DLLs load with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`;
  environment overrides are ignored when elevated; helper processes run by absolute path.
- Snapshot, restore and watch refuse reparse points; profile values are clamped.
- ASLR + DEP + high-entropy VA (`DllCharacteristics 0x0160`); this mingw toolchain emits no
  stack canaries (documented in SECURITY.md).

Threat model, findings F1–F9 and residual risks: [SECURITY.md](SECURITY.md).

## Status

Tested on one machine: Arc A770 16 GB, driver 32.0.101.9150, Windows build 26100
(Windows 11 24H2 / Server 2025). Windows 10 is supported in code (no Windows 11-only APIs,
version fallbacks for older driver stacks) but has not been run on real hardware yet — if
you try it, open an issue with the output of `arctray.exe --selftest`.

| Measured | Value |
|---|---|
| Tray working set | 16 MB (with the hardware handle released: 8.5 MB) |
| `--gfx-watch` process | 23 MB |
| Binary | ~91 KB, static |
| Power limit 398 W requested | accepted and read back; draw follows the limit into the low 300s (330 W peak measured), no fixed 289 W wall |
| Overclock ladder (llama-bench SYCL) | +150 MHz/+100 mV → 2752 MHz @ 342 W · +200/+150 → 2800 MHz @ 345 W · +299/+287 → TDR |

## License

GPL-2.0. Structure layouts, the 115 °C runtime route and the flat-table fan mechanism are
ported from [Arc-Power](https://github.com/YamsSE/Arc-Power) by YamsSE (GPL-2.0). Intel IGCL
and Level Zero Sysman are Intel APIs; this project is not affiliated with or endorsed by
Intel. See [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Links

- GitHub — https://github.com/gotofuckinghell/arc-tray
- GitLab — https://gitlab.com/grubyrams/arc-tray
- Telegram — https://t.me/hwretard (`/HW/ RETARDS`)
