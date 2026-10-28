# ArcTray

A headless tuning tray for **Intel Arc** GPUs: one 58 KB native Windows exe, a tray
menu, no Electron, no window, no service, no network.

```
right-click the tray icon
├── Arc A770 — 398 W · 2400 MHz · 52 °C
├── Profiles ▸ Gaming · Quiet · Stock
├── Power limit (398 W) ▸ 115 … 420 W
├── Core offset (+74 MHz) ▸ 0 … +300 MHz
├── Temperature limit (90 °C) ▸ 60 … 115 °C
├── Voltage offset (0 mV) ▸ 0 … +288 mV
├── Fan ▸ Auto · 30 … 100 %
├── Start with Windows
├── Links ▸ GitHub · GitLab · Telegram
└── About ArcTray… · Exit
```

Idle footprint: **8.7 MB working set / 1.5 MB private**. The GPU libraries are opened
lazily when the menu is shown and released 20 s later, so the idle tray costs almost
nothing.

## What it does

| Control | Range | How |
|---|---|---|
| Power limit | 115…420 W | Level Zero Sysman `zesPowerSetLimits` (sustained + burst), verified by read-back |
| Core clock offset | 0…+300 MHz | IGCL `ctlOverclockGpuFrequencyOffsetSetV2`, V1 fallback |
| Temperature limit | 60…115 °C | 60…90 through the driver runtime, 91…115 through the bundled 2023 IGCL runtime |
| Voltage offset | 0…+288 mV | IGCL `ctlOverclockGpuMaxVoltageOffsetSetV2` (positive only) |
| Fan | auto / 30…100 % | flat 2-point table via `ctlFanSetSpeedTableMode` (the A770 rejects the fixed-speed API) |
| Profiles | — | `arctray.ini` next to the exe |

Every write is read back and the driver's answer is shown in a balloon instead of a
fake "OK". Failed applies report the exact IGCL / Level Zero result code.

## Requirements

- Windows 10 1607+ or Windows 11, **x64**
- Intel Arc GPU with a driver that ships IGCL (`IntelControlLib.dll`) and, for the
  power limit, Level Zero Sysman (`ze_loader.dll`)
- Administrator rights for writes (a non-elevated IGCL overclock write returns
  SUCCESS and then reverts)
- No redistributables: the exe imports only KERNEL32/USER32/SHELL32 plus the UCRT
  API sets that are part of Windows 10/11

## Install (portable)

1. Put `arctray.exe` in a folder, e.g. `C:\Program Files\ArcTray` (see the note below).
2. Optional: `arc.ico` next to it for the tray icon.
3. Optional: `IntelControlLib-2023.dll` next to it — only needed for temperature
   limits above 90 °C. That is Intel's 2023 IGCL runtime, redistributed by the
   [Arc-Power](https://github.com/YamsSE/Arc-Power) project; this repository does not
   ship Intel binaries.
4. Run it elevated once and tick **Start with Windows**. The app creates the logon
   task `ArcTray Elevated` (`/SC ONLOGON /RL HIGHEST`, InteractiveToken), so the tray
   starts with rights and no UAC prompt.

`arctray.ini` is created next to the exe; if the folder is read-only the app falls
back to `%APPDATA%\ArcTray\arctray.ini`.

> **Install location matters.** An elevated tray started from a user-writable folder
> is a local privilege-escalation vector: whoever can write `arctray.exe` gets code
> execution as administrator at logon. Use a folder writable by administrators only,
> or do not enable the logon task. See [SECURITY.md](SECURITY.md).

## CLI

```
arctray.exe --state                     current caps + live values
arctray.exe --set pl=398 core=75 temp=95 fan=60
arctray.exe --set fan=auto
arctray.exe --set volt=50               voltage offset, mV (positive only)
arctray.exe --profile=Gaming            apply an ini profile
arctray.exe --autostart=on|off|status   logon task
arctray.exe --watch[=sec]               real board power (W) from the IGCL energy counter
arctray.exe --selftest                  OS build, driver, which IGCL/Sysman symbols exist
arctray.exe --caps                      Sysman V/F capabilities and current targets
arctray.exe --about                     version, attribution, links
arctray.exe --menudump                  print the menu tree (labels + ids)
```

## Config

```ini
; profile=Name,PowerW,CoreOffsetMHz,VoltOffsetmV,TempC,Fan(auto|30..100)
profile=Gaming,375,0,0,90,auto
profile=Quiet,150,0,0,85,auto
profile=Stock,230,0,0,90,auto
```

## Build

Requires an x64 mingw-w64 gcc (Strawberry Perl ships one as
`x86_64-w64-mingw32-gcc`) and Python 3 for the icon generator.

```bash
bash build.sh        # -> arctray.exe + arc.ico
```

## How it works

- `src/arc.h` — IGCL and Level Zero Sysman structure layouts, each with a
  `_Static_assert` on its size (36/320/440/936/1016/1024/80 bytes).
- `src/arc.c` — DLL discovery, init, state read, apply paths, the extended 2023
  runtime for the high temperature range.
- `src/tray.c` — Win32 tray, menu, INI, CLI, autostart task.
- `src/probe*.c` — small read-only probes used to pin the structure versions on a
  live card; kept as documentation of what the driver actually reports.

Driver quirks that shaped the code (why the fan needs a table, why 115 °C needs an
extra runtime, why zeroing the voltage offset resets the other controls, why the
Sysman undervolt is disabled) are written up in [docs/A770-NOTES.md](docs/A770-NOTES.md).

## Robustness across driver branches

The tool has to survive older Intel stacks (the Windows 10 branch in particular):

- **IGCL discovery** — `ARCTRAY_IGCL` (non-elevated only) → DriverStore
  `*iigd*`/`*igfx*`/`*igdlh*` → `System32\IntelControlLib.dll` → Intel Graphics
  Software folder. `System32\ControlLib.dll` is never used (UID-whitelisted loader).
- **Structure versions** — `ctlGetDeviceProperties` walks Version 3 → 2 → 1 and
  `ctlOverclockGetProperties` walks 1 → 0.
- **Setters** — every V2 call falls back to the V1 symbol; the message says `(V1)`
  when that path was used.
- **Power limit** — without Level Zero Sysman the write goes through IGCL clamped to
  the advertised range (A770: 115…276 W) and says `ok (IGCL)`.
- **Telemetry** — `ctlPowerTelemetryGetV2` falls back to the V1 1024-byte structure.

Debug switches: `ARCTRAY_NO_SYSMAN=1`, `ARCTRAY_V1=1`, `ARCTRAY_PROPS_VERSION=1`.
DLL-path overrides are ignored in an elevated process by design (see SECURITY.md).

## Measuring a power limit

`arctray.exe --watch=20` prints board power 0.5 s apart, derived from the IGCL energy
counter (the counter is a **double in joules**; `gpuPowerPercent` is unsupported on
this stack). Measured on the A770 with a 398 W limit:

```
idle               39.5 W
llama-bench (SYCL, MiniCPM5-2B, -p 4096)   277…282 W, 2532 MHz, 71 °C, flags POWER CURRENT
```

So a real compute load does exceed the 160–180 W that FurMark and Superposition reach —
those two do not saturate Xe. The card stops at ~280 W with the **current** limit flag
set, which means the VRM/connector current limit binds before the 398 W register value
ever matters (2×8-pin + slot is 375 W on paper). Treat >300 W as unreachable on this
board rather than as a target.

## Status

Tested on one machine: Arc A770, driver 32.0.101.9150, Windows 11 24H2 (build 26100).
Windows 10 is supported in code (no Windows 11-only APIs, version fallbacks for older
driver stacks) but has not been run on real hardware yet — if you try it, please open
an issue with the output of `arctray.exe --selftest`.

## License

GPL-2.0. Structure layouts, the 115 °C runtime route and the flat-table fan mechanism
are ported from [Arc-Power](https://github.com/YamsSE/Arc-Power) by YamsSE (GPL-2.0).
Intel IGCL and Level Zero Sysman are Intel APIs; this project is not affiliated with
or endorsed by Intel. See [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Links

- GitHub — https://github.com/gotofuckinghell
- GitLab — https://gitlab.com/grubyrams
- Telegram — https://t.me/hwretard (`/HW/ RETARDS`)
