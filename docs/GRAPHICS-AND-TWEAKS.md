# Graphics / display controls and registry tweaks — catalogue and plan

Everything here was measured on the live Arc A770 (driver 32.0.101.9150) or taken from
the IGCL/Sysman surfaces the driver actually implements. It is the input for the next
ArcTray menu block; nothing in this file is applied by the current binary.

## 0. Implemented in 1.4.0

`src/gfx.c` + `--gfx` / `--gfx-set=<name>:<value>` + the **Graphics** tray submenu cover
the features whose values the driver actually stores on this card:

| name | id | type | values |
|---|---|---|---|
| `frame-limit` | 2 | INT32 | 0 (off), 30, 60, 90, 120, 144, 165, 240 |
| `flip-mode` | 9 | ENUM bitmask | 1 app-default, 2 vsync-off, 4 vsync-on, 8 smooth-sync, 16 speed-frame, 32 capped-to-panel |
| `low-latency` | 16 | ENUM | 0 off, 1 on, 2 on+boost |
| `frame-gen` | 17 | ENUM | 0 app-choice, 1 2x, 2 3x, 3 4x |

Measured round-trip on the A770: `flip-mode 2 → 2`, `low-latency 2 → 2`,
`frame-gen 1 → 1`, `frame-limit 60 → 60`, every write read back and restored.

Two layout facts the header settles: the value union differs per type
(`ENUM` is a single `uint32 EnableType` at the union start, `INT/UINT/FLOAT` are
`{bool Enable; value}`), and a feature that was never written answers
`0x40000014 ERROR_DATA_NOT_FOUND` — after the first write it reads back normally.

`cmaa`, `atomics-64`, `scope`, `app-profiles`, `live-state` are advertised by the caps
but return `DATA_NOT_FOUND`/`CUSTOM`-only shapes; they stay out of the menu until they
are understood.

## 1. What the A770 driver exposes that is not overclocking

`ctlGetSupported3DCapabilities` on this card returns **9 features** (probe:
`src/probe_3d.c`):

| # | Feature | Value type | Per-app | Meaning |
|---|---|---|---|---|
| 2 | `FRAME_LIMIT` | INT32 | yes | FPS cap the driver applies in the compositor |
| 4 | `CMAA` | UINT64 (enum/bool) | yes | Conservative morphological anti-aliasing |
| 9 | `GAMING_FLIP_MODES` | UINT64 (enum) | yes | **VSync / flip model**: application-default, vsync-on, vsync-off, smooth-sync, speed-frame, smart-vsync |
| 11 | `APP_PROFILES` | CUSTOM | yes | per-application profile blob |
| 13 | `EMULATED_64BIT_ATOMICS` | UINT64 (bool) | yes | emulated 64-bit atomics (compat switch for some D3D12 titles) |
| 15 | `GLOBAL_OR_PER_APP` | UINT64 (enum) | yes | **scope switch**: global vs per-application (read this first) |
| 16 | `LOW_LATENCY` | UINT64 (enum) | yes | **Xe Low Latency**: off, on, on-boost |
| 17 | `FRAME_GENERATION` | UINT64 (enum) | yes | **XeSS Frame Generation override**: app-choice, 2x, 3x, 4x |
| 19 | `LIVE_STATE` | CUSTOM | — | driver's live state blob |

Notes from the probe:

- `ctlGetSupported3DCapabilities` is two-pass: first call with
  `NumSupportedFeatures = 0` returns the count, second call needs **exactly** that
  count plus a pointer to the details array, otherwise `0x4000000f`
  `ERROR_INVALID_SIZE`.
- Reading a feature value (`ctlGetSet3DFeature`, `bSet = false`) returned
  `0x40000014 ERROR_DATA_NOT_FOUND` for every scope variant until the scope feature
  (15) is set — the implementation must set the scope first, exactly like IGS does.
- `ctl_3d_feature_getset_t` is 56 bytes: `Size, Version, FeatureType, ApplicationName*,
  ApplicationNameLength, bSet, ValueType, Value[8], CustomValueSize, CustomValue*`.
- `ctl_3d_feature_details_t` is 72 bytes and carries `PerAppSupport` and
  `ConflictingFeatures`, so the UI can grey out conflicting options instead of lying.

## 2. Display (output) controls

| Control | IGCL call | Values | Notes |
|---|---|---|---|
| Colour range / quantization | `ctlGetSetDisplaySettings` | default, limited, full | HDMI/DP output range |
| Wire format | `ctlGetSetWireFormat` | RGB, YCbCr420, YCbCr422, YCbCr444 | GET/SET/RESTORE_DEFAULT ops |
| Bits per channel | `ctlGetSetDisplaySettings` | 6, 8, 10, 12 | depends on the display's EDID |
| Scaling mode | `ctlSetCurrentScaling` | identity, centered, stretched, aspect-ratio-centered-max, custom | **physical modeset — the screen flashes** |
| Scaling method | same | integer, nearest-neighbour, maintain-display-scaling | integer scaling for pixel art |
| Retro scaling | `ctlGetSetRetroScaling` | integer, nearest-neighbour | same flash caveat |
| Arc Sync profile | `ctlGet/SetIntelArcSyncProfile` | recommended, excellent, good, compatible, off, vesa, custom | adaptive-sync tuning per monitor |
| Global VRR mode | `ctlGetSet3DFeature` (feature 14 `VRR_WINDOWED_BLT`) | fullscreen, fullscreen-windowed, disabled | note: this card's caps list does **not** include 14, so IGS's VRR switch is unavailable here |

## 3. Registry tweaks (separate submenu, with a one-line "what it does")

All four are reversible, need administrator rights, and are read back before they are
shown as on/off. Verified paths and values:

| id | Name | What it does | Keys |
|---|---|---|---|
| `mpo` | Multiplane Overlay | can cause stutter / flicker / black screens on some setups; enabling writes the legacy MPO workaround | `HKLM\SOFTWARE\Microsoft\DirectX\UserGpuPreferences` `MPOHack=1`, same under `HKCU`, and `HKLM\SOFTWARE\Microsoft\Windows\Dwm` `OverlayTestMode=5` |
| `hags` | Hardware-accelerated GPU scheduling | lets the GPU scheduler manage VRAM instead of the CPU driver thread — lower latency on some systems, stutter on others; **reboot required** | `HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers` `HwSchMode=2` (on) / `1` (off) |
| `game-dvr` | Game DVR / background recording | turns off Game Bar background recording machine-wide (a few percent of FPS) | `HKLM\SOFTWARE\Policies\Microsoft\Windows\GameDVR` `AllowGameDVR=0` |
| `fullscreen-optimizations` | Fullscreen Optimizations (per-app) | per-app compatibility flag; some games stutter with FSO on. No system-wide switch | `HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers` value `<exe path>` = `~ FULLSCREENOPTIMIZATIONS` |

Revert = delete the value (or restore the previous data), which is what Arc-Power does
and what the menu should offer as a third action.

## 4. Menu layout to embed

```
ArcTray ▸
├── <state header>
├── Profiles ▸
├── Power limit ▸ / Core offset ▸ / Temperature limit ▸ / Voltage offset ▸ / Fan ▸
├── Graphics ▸            (IGCL 3D features — global scope)
│   ├── FPS limit ▸  off · 30 · 60 · 90 · 120 · 144 · 165 · 240
│   ├── VSync / flip mode ▸  app-default · on · off · smooth-sync · speed-frame · smart-vsync
│   ├── Low latency ▸  off · on · on-boost
│   ├── Frame generation ▸  app-choice · 2x · 3x · 4x
│   └── 64-bit atomics emulation ▸  off · on
├── Display ▸             (output settings; scaling flashes the screen)
│   ├── Colour range ▸ default · limited · full
│   ├── BPC ▸ 6 · 8 · 10 · 12
│   ├── Scaling ▸ identity · centered · stretched · AR-centered · custom
│   └── Integer scaling ▸ off · integer · nearest-neighbour
├── Tweaks ▸              (registry, admin, reboot flags shown)
│   ├── ☑/☐ MPO workaround            — "stops MPO stutter/flicker (experimental)"
│   ├── ☑/☐ GPU scheduling (HAGS)     — "needs reboot"
│   ├── ☑/☐ Game DVR                  — "disables background recording"
│   └── ☑/☐ Fullscreen optimizations  — per-app list
├── Start with Windows
└── About ▸
    ├── About ArcTray...
    ├── GitHub — gotofuckinghell
    ├── GitLab — grubyrams
    └── Telegram — /HW/ RETARDS
```

The About submenu is already implemented (v1.2.1 layout); the rest is the next block.

## 4b. Latency tweaks — implemented in v1.3.0

These live in `src/tweaks.c` (table + read/write helpers) and are exposed as
**Tweaks (latency)** in the tray and as `--tweaks` / `--tweak=<id>:on|off|revert` on the
command line. Every entry is a plain HKLM/HKCU value, state is read back before the tick
is drawn, and "Revert" deletes the values (Windows defaults return).

| id | Menu name | Values written (ON) | Reboot |
|---|---|---|---|
| `hags` | Hardware-accelerated GPU scheduling | `HwSchMode=2`, OFF `1` | yes |
| `mpo` | Multiplane Overlay workaround | `MPOHack=1` (HKLM+HKCU), `Dwm\OverlayTestMode=5`, OFF `0` | helps |
| `gamedvr` | Game DVR / background recording | `GameDVR_Enabled=0`, `AllowGameDVR=0`, OFF `1` | no |
| `fso` | Fullscreen optimizations (global default) | `GameDVR_FSEBehaviorMode=2`, OFF `0` | no |
| `mmcss-games` | MMCSS "Games" task priority | `GPU Priority=8`, `Priority=6`, `Scheduling Category=High`, `SFIO Priority=High` (OFF: 1/2/Medium/Normal) | no |
| `system-responsiveness` | MMCSS system responsiveness (0%) | `SystemResponsiveness=0`, OFF `20` | no |
| `net-throttling` | Network throttling limit off | `NetworkThrottlingIndex=0xFFFFFFFF`, OFF `10` | no |
| `timer-resolution` | Global high-resolution timers | `Session Manager\kernel\GlobalTimerResolutionRequests=1`, OFF absent | yes |
| `win32-priority` | Win32PrioritySeparation (foreground boost) | `0x26`, OFF `0x2` (Windows default) | yes |

Measured round-trip on this machine (MMCSS): ON wrote `GPU Priority=8`, `Priority=6`,
`Scheduling Category=High`, `SFIO Priority=High` (verified with PowerShell), revert
deleted all four and the menu went back to OFF.

Sources for the values: NicholasBly/Windows-11-Latency-Optimization `.reg`,
jinshitweaks MMCSS reference, Blur Busters / Guru3D timer-resolution threads,
Arc-Power `registry-catalog.js` (MPO/HAGS/GameDVR/FSO).

## 5. Implementation notes for ArcTray

- Reuse `src/arc.h`: add `ctl_3d_feature_caps_t` (24), `ctl_3d_feature_details_t` (72),
  `ctl_3d_feature_getset_t` (56), plus the display structs already declared
  (`ctl_display_settings_t`, `ctl_get_set_wire_format_config_t`, `ctl_scaling_*`).
- Read order for graphics: caps (`ctlGetSupported3DCapabilities`, exact count on the
  second call) → scope (`GLOBAL_OR_PER_APP`) → per-feature get; a feature missing from
  caps must not be listed at all (no dead menu items).
- Every write: set → `Sleep(150)` → get → compare; report the driver's answer instead
  of a fake OK, same contract as the OC controls.
- Scaling / wire format / BPC change the physical output: mark those entries with a
  warning line in the menu and never apply them from a profile automatically.
- Registry tweaks: run `reg.exe` through `CreateProcessW` with the full System32 path
  (the same lesson as `schtasks`), read state with `reg query` before showing a tick,
  and label reboots where the driver needs one (HAGS).
- All of this is admin-only; the tray already runs elevated from the logon task.

## 6. Test plan

1. `--gfx` (new CLI): print caps + current values, no writes — compare against IGS.
2. Flip mode: set `vsync-off`, verify in a fullscreen game (tearing) and read back.
3. FPS limit: set 60, confirm with a frame counter; then `off`.
4. Registry: toggle each tweak, confirm the value with `reg query`, revert, confirm
   deletion. HAGS needs a reboot to prove.
5. Scaling: apply `integer` on a 1080p display, watch for the modeset flash, revert.
