# CLI reference

Every flag runs without creating a window and can be used from `cmd.exe`, PowerShell or a
script. Reads work unelevated; writes need an elevated shell (otherwise IGCL answers
`ERROR_OC_INSUFFICIENT_PERMISSIONS` or reports success and reverts).

Exit codes: `0` success, `1` driver refused / verification failed, `2` usage error.

## Tuner

```
arctray.exe --state
arctray.exe --set <key=value> [<key=value> ...]
```

`--state` prints the adapter, elevation state, whether Sysman is usable, and the current
power limit, core offset, voltage offset, temperature limit, VRAM clock/temperature and
live telemetry (clock, temperature, fan RPM).

`--set` accepts these keys, applied in order:

| Key | Unit | Range on an A770 |
|---|---|---|
| `pl` | watts | 115…420 (IGCL advertises 115…276; Sysman bypasses that) |
| `core` | MHz offset | 0…300 |
| `volt` | mV offset | 0…288 (positive only; 0 clears the offset and restores PL/core/temp) |
| `temp` | °C | 60…115 (above 90 needs `IntelControlLib-2023.dll`) |
| `fan` | `auto` or percent | 30…100 |

```bash
arctray.exe --set pl=350 core=150 volt=100 temp=85
arctray.exe --set fan=auto
```

Each write is followed by a read-back; the output shows the resulting value and which path
was used, e.g. `pl=350: 350 W ok (Sysman)`, `temp=95: temp 95 C ok (ext)`.

## Profiles

```
arctray.exe --profile=<name>
```

Applies the matching `profile=` line from `arctray.ini`:

```ini
; profile=Name,PowerW,CoreOffsetMHz,VoltOffsetmV,TempC,Fan(auto|30..100)
profile=Gaming,375,0,0,90,auto
```

Values are clamped to the advertised ranges (clamps are written to `arctray.log`).

## Telemetry

```
arctray.exe --watch[=<seconds>]
```

Prints board power once per second (default: until Ctrl+C) derived from the IGCL energy
counter, plus clock and temperature. The counter is a double in joules; `gpuPowerPercent`
is unsupported on this driver.

## Graphics features

```
arctray.exe --gfx
arctray.exe --gfx-set=<name>:<value>
arctray.exe --gfx-save[=<file>]
arctray.exe --gfx-restore[=<file>]
arctray.exe --gfx-watch[=<seconds>]
```

`--gfx` lists every advertised feature with its type, per-app flag and current value;
features that were never written show `n/a`.

`--gfx-set` writes one feature and reads it back:

| Name | Id | Values |
|---|---|---|
| `frame-limit` | 2 | 0, 30, 60, 90, 120, 144, 165, 240 |
| `cmaa` | 4 | 0, 1 |
| `flip-mode` | 9 | 1 app default, 2 vsync off, 4 vsync on, 8 smooth sync, 16 speed frame, 32 capped to panel |
| `atomics-64` | 13 | 0, 1 |
| `low-latency` | 16 | 0 off, 1 on, 2 on + boost |
| `frame-gen` | 17 | 0 app choice, 1 2×, 2 3×, 3 4× |

```bash
arctray.exe --gfx-set=low-latency:2
arctray.exe --gfx-set=frame-limit:144
arctray.exe --gfx-set=flip-mode:2      # force vsync off
```

`--gfx-save` writes every readable feature to
`%APPDATA%\ArcTray\gfx-snapshot.ini` (or the file given after `=`):

```
# name id enable value
frame-limit 2 1 240
cmaa 4 0 0
flip-mode 9 1 2
atomics-64 13 1 1
low-latency 16 1 2
frame-gen 17 0 0
```

`--gfx-restore` writes each line back and verifies the read-back:

```
  low-latency    -> 2 : written, read back ok
  flip-mode      -> 2 : written, read back ok
restored 6, problems 0
```

`--gfx-watch` appends every change to `%APPDATA%\ArcTray\gfx-watch.log` (default interval
30 s, minimum 5):

```
2026-10-29 12:10:41 watch start: 9 features, interval 30 s
2026-10-29 12:10:41   baseline low-latency    enable=1 value=2
2026-10-29 12:09:57 low-latency    1/2 -> 1/1
2026-10-29 12:13:41 heartbeat (120 polls)
```

It also logs `became readable` / `now unreadable` transitions and rotates to
`gfx-watch.log.1` at 1 MB. Run it detached to keep a history:

```powershell
Start-Process arctray.exe -ArgumentList '--gfx-watch=30' -WindowStyle Hidden
```

## Registry tweaks

```
arctray.exe --tweaks
arctray.exe --tweak=<id>:on|off|revert
```

`--tweaks` prints the table with the current state:

```
hags                   ON   Hardware-accelerated GPU scheduling  (reboot)
mpo                    ON   Multiplane Overlay (MPO) workaround
gamedvr                ON   Game DVR / background recording
fso                    ON   Fullscreen optimizations (global default)
mmcss-games            OFF  MMCSS "Games" task priority
system-responsiveness  ON   MMCSS system responsiveness (0%)
net-throttling         ON   Network throttling limit off
timer-resolution       ON   Global high-resolution timers  (reboot)
win32-priority         OFF  Win32PrioritySeparation (foreground boost)  (reboot)
```

`--tweak=hags:on`, `--tweak=mpo:off`, `--tweak=fso:revert`. HKLM tweaks need an elevated
shell.

## Service

```
arctray.exe                              tray icon
arctray.exe --autostart=on|off|status
arctray.exe --selftest
arctray.exe --caps
arctray.exe --menudump
arctray.exe --about
```

- `--autostart=on` creates the task `ArcTray Elevated` (`ONLOGON`, `Highest Available`);
  `status` queries the task itself rather than a registry flag.
- `--selftest` prints OS build, driver, DLL path, elevation and which IGCL/Sysman symbols
  were resolved — the first thing to attach to a bug report.
- `--caps` prints advertised ranges, Sysman availability and current targets.
- `--menudump` prints the tray menu tree with ids (debugging).
- `--about` prints the version, attribution and links.

## Environment variables

Developer switches, ignored for DLL selection when the process is elevated:

| Variable | Effect |
|---|---|
| `ARCTRAY_IGCL` | use this IGCL DLL path (non-elevated only) |
| `ARCTRAY_IGCL2023` | same for the 2023 runtime |
| `ARCTRAY_ALLOW_UNSIGNED=1` | load the 2023 runtime even when its SHA-256 does not match the pinned value |
| `ARCTRAY_NO_SYSMAN=1` | skip Level Zero Sysman (power limit then goes through IGCL) |
| `ARCTRAY_V1=1` | force the V1 setters |
| `ARCTRAY_PROPS_VERSION=1..3` | force a structure version |
| `ARCTRAY_TWEAK_DEBUG=1` | verbose registry tracing |
