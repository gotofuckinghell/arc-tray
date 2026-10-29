# Arc controls reference — what exists, what it does, what this driver answers

Sources, in order of trust: `igcl_api.h` (official enums with Intel comments), the shipped
binaries (`IntelControlLib.dll`, `igdkmdnd64.sys`, `igdumdim64.dll`), the driver INF
defaults, the live registry on this machine (Arc A770, driver 32.0.101.9150, Win 26100),
then public snapshots of the same keys (`nohuto/regkit`, `djdallmann/GamingPCSetup`,
`shoober420/windows11-scripts`, `orev/dpst-control`).

Result codes used below: `0x0` success, `0x4000000A` `ERROR_UNSUPPORTED_FEATURE`,
`0x40000014` `ERROR_DATA_NOT_FOUND` (never written yet).

## 1. Tuner (implemented in ArcTray)

| Control | API | Range on the A770 | Notes |
|---|---|---|---|
| Power limit | Sysman `zesPowerSetLimits`, IGCL V2 as fallback | 115–398 W accepted | real draw tracks the limit into the low 300s (330 W peak measured); PL4/OCP is a protection trip, not a fixed ceiling |
| Core offset | `ctlOverclockGpuFrequencyOffsetSetV2` → V1 | 0…+300 MHz | offset shifts the whole V/F curve, not a cap; readback within ±12 MHz of the driver grid |
| Voltage offset | `ctlOverclockGpuMaxVoltageOffsetSetV2` | 0…+288 mV | positive only; writing 0 mV resets **all** IGCL overclock controls, so PL/core/temp are snapshotted and restored |
| Temperature limit | IGCL V2 (60–90 °C), bundled 2023 runtime (91–115 °C) | 60–115 °C | the driver refuses >90; the 2023 runtime is unsigned and hash-pinned |
| Fan | `ctlFanSetSpeedTableMode` with a flat 2-point table | auto, 30–100 % | the fixed-speed API returns `ERROR_UNSUPPORTED_FEATURE` on this board |
| VRAM clock | — | fixed 2187 MHz / 17.5 Gb/s | no IGCL control (`unsupported`), needs register/vBIOS work |
| ICC max | Sysman `ZES_OVERCLOCK_CONTROL_ICCMAX_LIMIT` | dead | `zesDeviceEnumOverclockDomains` → `0x78000003`, `AvailableControls=0` |
| V/F curve | Sysman VF pair | not implemented | writing it re-bases the operating point (+75 MHz read back as +224 MHz) |

## 2. 3D features — all 21 ids

Availability column = live probe on driver 9150 (`src/probe_features.c`).

| Id | Name | Values (header) | Status | What it does |
|---|---|---|---|---|
| 0 | `FRAME_PACING` | enum | `UNSUPPORTED_FEATURE` | driver-side pacing of presents (smoothness at the cost of a little latency) |
| 1 | `ENDURANCE_GAMING` | int FPS | `UNSUPPORTED_FEATURE` | caps FPS **in DC (battery) mode** to save power; A770 is a desktop card, hence absent |
| 2 | `FRAME_LIMIT` | int FPS | works | max FPS independent of power state |
| 3 | `ANISOTROPIC` | 0 app, 2×, 4×, 8×, 16× | `UNSUPPORTED_FEATURE` | forces anisotropic filtering |
| 4 | `CMAA` | 0 off, 1 override MSAA, 2 enhance app | works | morphological AA; `override` replaces app MSAA |
| 5 | `TEXTURE_FILTERING_QUALITY` | 0 performance, 1 balanced, 2 quality | `UNSUPPORTED_FEATURE` | driver texture-filter quality |
| 6 | `ADAPTIVE_TESSELLATION` | 0 off, 1 on | `UNSUPPORTED_FEATURE` | reduces tessellation where it is invisible |
| 7 | `SHARPENING_FILTER` | 0 off, 1 on | `UNSUPPORTED_FEATURE` | driver sharpening (Intel's RIS analogue) |
| 8 | `MSAA` | 0 app, 1 off, 2×, 4×, 8×, 16× | `UNSUPPORTED_FEATURE` | forced MSAA |
| 9 | `GAMING_FLIP_MODES` | bitmask: 1 app default, 2 vsync off, 4 vsync on, 8 smooth sync, 16 speed frame, 32 capped to panel, 64 vsync-off ignoring allow-list | works | presentation model: `vsync off` converts sync flips to async, `smooth sync` reduces tearing with async flips, `speed frame` = application-unaware triple buffering, `capped` limits FPS to panel rate |
| 10 | `ADAPTIVE_SYNC_PLUS` | custom struct | `UNSUPPORTED_FEATURE` | VRR range/behaviour override |
| 11 | `APP_PROFILES` | custom payload | needs buffer | per-game compatibility profile blobs |
| 12 | `APP_PROFILE_DETAILS` | custom payload | needs buffer | per-game profile tier details |
| 13 | `EMULATED_TYPED_64BIT_ATOMICS` | enum | works | DG2 workaround for D3D12 titles that need 64-bit atomics |
| 14 | `VRR_WINDOWED_BLT` | enum | `UNSUPPORTED_FEATURE` | VRR for windowed games |
| 15 | `GLOBAL_OR_PER_APP` | 0 none, 1 per-app, 2 global | `DATA_NOT_FOUND` | selects the namespace for the other features |
| 16 | `LOW_LATENCY` (XeLL) | 0 off, 1 on, 2 on+boost | works | latency reduction (Reflex analogue), **not** resolution |
| 17 | `FRAME_GENERATION` | 0 app, 1 2×, 2 3×, 3 4× | works | XeSS frame generation override |
| 18 | `PREBUILT_SHADER_DOWNLOAD` | bool | `UNSUPPORTED_FEATURE` | lets the driver fetch compiled shaders instead of compiling in-game |
| 19 | `LIVE_STATE` | custom | `DATA_NOT_FOUND` | per-app live state (used by overlays) |
| 20 | `FRAME_GENERATION_CONTROL` | enum | `UNSUPPORTED_FEATURE` | extra FG controls, not advertised on this stack |

Consequence: on 9150 only six features are writable (2, 4, 9, 13, 16, 17) plus the two
CUSTOM profile ids that need a payload. Everything the "tweak lists" promise about MSAA,
anisotropy and sharpening is not implemented in this driver build at all — it is not a
hidden switch, it returns `UNSUPPORTED_FEATURE` before any write.

## 3. Video processing (media pipeline, not games)

`ctlGetSetVideoProcessingFeature`:

| Id | Feature | Notes |
|---|---|---|
| 0 | `FILM_MODE_DETECTION` | inverse telecine for video |
| 1 | `NOISE_REDUCTION` | temporal/spatial NR |
| 2 | `SHARPNESS` | video sharpening |
| 3 | `ADAPTIVE_CONTRAST_ENHANCEMENT` | ACE for video |
| 4 | `SUPER_RESOLUTION` | **video** upscaler; flags: default, conference, camera capture; disabled by default, "contact Intel for activation" |
| 5 | `STANDARD_COLOR_CORRECTION` | hue/saturation/brightness/contrast |
| 6 | `TOTAL_COLOR_CORRECTION` | full colour pipeline |
| 7 | `SKIN_TONE_ENHANCEMENT` | skin-tone correction |

`SUPER_RESOLUTION` is not a VSR analogue: the enum names conference calls and security
cameras. It enhances a video stream; the render resolution of a game never touches it.

## 4. Display settings and sharpening

- `ctlGetSetDisplaySettings` — scaling mode (`identity`, `centered`, `stretched`,
  `aspect-ratio centered`, custom), colour format, bpc, wire format. This is **output**
  scaling; it cannot make the GPU render above native and downsample.
- `ctlGetSharpnessCaps` / `ctlGetCurrentSharpness` / `ctlSetSharpness` — panel-side
  sharpening (affects everything on screen, including games, but does not change
  resolution).
- `ctlGetSetRetroScaling` — integer/retro scaling for low-resolution content.
- `ctlGetLACEConfig` — Local Adaptive Contrast Enhancement.
- `ctlGetSetDynamicContrastEnhancement` — dynamic contrast for video.
- `ctl_display_setting_low_latency_t` (`DEFAULT`/`DISABLED`/`ENABLED`) — panel low-latency
  signalling for HDR10+ Gaming. Different thing from XeLL (id 16).

There is no DSR/VSR-style "render at 2× and downsample" toggle anywhere in IGCL. The
closest practical routes are in-game resolution scale, a custom higher resolution mode, or
XeSS (which upscales instead of supersamples).

## 5. Registry — what the driver actually reads

### Adapter class key (live values on this A770)

`HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0000`

| Value | Now | Meaning | Touch? |
|---|---|---|---|
| `Display1_DisableAsyncFlips` | 0 | 1 disables async flips on pipe 1 (less tearing, slightly more latency) | yes, low risk |
| `Disable_OverlayDSQualityEnhancement` | 0 | 1 takes the non-enhanced overlay/MPO path | yes, low risk |
| `IGCLFrameGeneration` | 1 | exposes XeSS-FG through IGCL | medium |
| `IGCLGamingDisableControl` | 8 | bitmask that hides parts of the IGCL gaming surface (only string reference lives in `IntelControlLib.dll`) | no — high risk |
| `FeatureTestControl` | `0x1200` | driver test bitfield; **bit 5 (0x20) is DPST** (panel power saving) per `orev/dpst-control`: 0 = DPST enabled, 1 = disabled | only for DPST |
| `DisplayFeatureControl` | `0x1003E7` | display feature bitfield (KMD references it 3×) | no |
| `DisplayPcFeatureControl` | `0x50C0000` | second display feature bitfield | no |
| `PowerAcPolicy` / `PowerDcPolicy` | `0xC0A00020` | AC/DC power policy bitmask (KMD references it) | no |
| `TurboAcPolicy` / `TurboDcPolicy` | 1 | turbo policy | medium |
| `HwQueuedRenderPacketGroupLimitPerNode` | BINARY | scheduler packet-group limit, per adapter copy | medium |
| `EnableUnderRunInterrupt` | 1 | display underrun interrupt | low |
| `AdaptiveVsyncEnable` | 1 | adaptive VSync default | low |

### Global keys

| Key | Value | Meaning |
|---|---|---|
| `…\Control\GraphicsDrivers\HwSchMode` | 2 on / 1 off | hardware-accelerated GPU scheduling (HAGS); changes frame pacing on Arc |
| `…\Control\GraphicsDrivers\Disable_OverlayDSQualityEnhancement` | 1 | global overlay/MPO workaround |
| `…\Control\GraphicsDrivers\Scheduler\HwQueuedRenderPacketGroupLimit` | 2 | queued render packet groups; the community note in `shoober420/windows11-scripts` says **1 decreases FPS**, 2 is the sane value |
| `…\Control\GraphicsDrivers\Scheduler\HwQueuedRenderPacketGroupLimitPerNode` | 1 | same, per node |
| `HKLM\SOFTWARE\Microsoft\DirectX\DisableHWOverlay` | 1 | force the non-overlay composition path |
| `HKLM\SOFTWARE\Microsoft\Windows\Dwm\OverlayTestMode` | 5 | classic MPO-off switch |

`FlipQueueSize` is an NVIDIA/D3D9 key; the Intel driver has no such value. Arc Control has
no "pre-rendered frames" either — depth is application-side (frames in flight) plus the
scheduler keys above.

## 6. Dead ends confirmed on this driver

- 11 of the 21 3D feature ids answer `ERROR_UNSUPPORTED_FEATURE` (see table); `scope` and
  `live-state` answer `DATA_NOT_FOUND`.
- ICC/current limit: Sysman overclock domains unusable (`0x78000003`, mask `0`).
- V/F curve write re-bases clocks instead of adjusting them.
- VRAM frequency/voltage: no API; "unsupported".
- I2C to the VRM: `ctlEnumerateI2CPinPairs` returns 5 pairs, every transaction is
  `ERROR_KMD_CALL` because `Intel_NF_I2C.sys` (device `VIDEO\INTC_I2C`) is not installed.
- Peak power limits: 800 W AC/DC, writing them changes nothing.

## 7. What is still unknown, and how to close it

| Unknown | How to resolve |
|---|---|
| bit meaning of `IGCLGamingDisableControl` (currently 8) | flip single bits on a scratch driver state and observe which IGCL features disappear; only on a card you can reboot |
| `DisplayFeatureControl` / `DisplayPcFeatureControl` bits | compare against a B580 key dump, then toggle one bit at a time with a display attached |
| `PowerAcPolicy = 0xC0A00020` | correlate with `PowerAcPolicy` dumps from other Arc boards; the INF has the default, not the semantics |
| effect of scheduler queue values | PresentMon (`msUntilDisplayed`, `msBetweenPresents`, 1 % low) at 1 / 2 / 3 |

Everything above is documentation-level knowledge: no performance claim in this file has
been measured yet.
