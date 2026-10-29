# Arc registry / file tweaks — research notes (2026-10-29)

Scope: what actually exists for Intel Arc (A770, B570/B580, B770) beyond the official
controls — registry keys, scheduler knobs, file-level tricks — and what is a myth carried
over from NVIDIA. Everything marked **verified here** was read from this machine
(Arc A770 16 GB, driver 32.0.101.9150, Windows build 26100) or from a primary source.

## 1. The adapter class key (verified here)

`HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0000`

Values present on the A770, with their current contents:

| Value | Type / current | What it is | Risk if poked |
|---|---|---|---|
| `IGCLGamingDisableControl` | DWORD `8` | bitmask that disables parts of the IGCL gaming surface | high — the Graphics menu reads through that surface |
| `IGCLFrameGeneration` | DWORD `1` | enables XeSS frame-generation control through IGCL | medium |
| `Display1_DisableAsyncFlips` | DWORD `0` | `1` disables asynchronous flips | low — reverts on driver reinstall |
| `Disable_OverlayDSQualityEnhancement` | DWORD `0` | overlay/MPO quality path; the community "MPO fix" sets `1` | low |
| `FeatureTestControl` | DWORD `0x1200` | driver test bitfield (INF default) | high — undocumented bits |
| `DisplayFeatureControl` | DWORD `1049575` (`0x1003E7`) | display feature bitfield | high |
| `DisplayPcFeatureControl` | DWORD `84672512` (`0x50C0000`) | display feature bitfield #2 | high |
| `PowerAcPolicy` / `PowerDcPolicy` | DWORD `3231760544` (`0xC0A00020`) | AC/DC power policy bitmask | high |
| `TurboAcPolicy` / `TurboDcPolicy` | DWORD `1` | turbo policy | medium |
| `HwQueuedRenderPacketGroupLimitPerNode` | BINARY (present) | packet group limit for the GPU scheduler node | medium |
| `EnableUnderRunInterrupt` | DWORD `1` | underrun interrupt on display pipe | low |
| `AdaptiveVsyncEnable` | DWORD `1` | adaptive VSync default | low |
| `OpenGLFlags` | DWORD `3` | OpenGL driver flags | high |

The same set of value names appears in public snapshots of the class key
(`nohuto/regkit`, `djdallmann/GamingPCSetup` findings file), which confirms the names are
driver-wide, not machine-specific.

## 2. Global graphics keys that matter more than the class key

`HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers`

| Value | Values | Effect |
|---|---|---|
| `HwSchMode` | `2` = HAGS on, `1` = off | hardware-accelerated GPU scheduling; on Arc it changes frame pacing, and there are reports of OBS/encode stutter with HAGS on (B580) |
| `Disable_OverlayDSQualityEnhancement` | `1` | global MPO workaround |
| `Scheduler\HwQueuedRenderPacketGroupLimit` | `2` in shoober420's script | queued render packets per node — the closest thing Arc has to a queue-depth knob |
| `Scheduler\HwQueuedRenderPacketGroupLimitPerNode` | `1` (commented variant) | same, per node |

`HKLM\SOFTWARE\Microsoft\DirectX` → `DisableHWOverlay` = `1` — another way to force the
non-overlay composition path.

`HKLM\SOFTWARE\Microsoft\Windows\Dwm` → `OverlayTestMode` = `5` — the classic MPO-off
switch; not Intel-specific but it is the one people actually use on Arc.

## 3. Queues and latency — what exists and what does not

- **`FlipQueueSize`** (`...\Direct3D\FlipQueueSize`) is an NVIDIA/D3D9-era key. The Intel
  driver does not read it; copying NVIDIA tweak lists to Arc does nothing.
- **Pre-rendered frames**: Arc Control has no such setting; the request is open in the
  community issue tracker (IGCIT). Depth is decided by the application (DX12 frames in
  flight) plus the DWM/GPU scheduler.
- **Xe Low Latency (XeLL)** is a driver feature toggled per game in Intel Graphics
  Software, not by a registry value. Through IGCL it is feature id **16**
  (`OFF`/`ON`/`ON_BOOST`), which ArcTray already writes, and the flip-mode bitmask
  (id 9) covers `vsync off`, `smooth sync`, `speed frame`, `capped to panel rate`.
- **Async flips** (`Display1_DisableAsyncFlips`) trade a little latency for fewer
  tearing/stutter artefacts on some displays; test per monitor.

## 4. Files

- Intel Graphics Software keeps its data store in `C:\ProgramData\Intel\IGSDS`; on this
  machine it contains only `PrebuiltShaderBinaries` (shader cache), no per-game profile
  JSON. The IGS service is not running here (package 26.36.2702.2 is installed, service
  absent), so there is nothing to hand-edit — per-game profiles live in the app's own
  store when the full IGS is installed.
- `C:\ProgramData\Intel\IGSDS\PrebuiltShaderBinaries` is a safe cache to clear when a
  driver update leaves stale shaders behind.
- Driver defaults come from the INF `AddReg` sections (`FeatureTestControl=0x1200`,
  `PowerAcPolicy`, `TurboAcPolicy` …); the INF is the honest reference for "what is a
  default" before touching a value.
- `igfxtweak` (nitrocaster) is the only public tool that touches *display* registers
  directly (dithering/FRC on 6-bit panels) — an MMIO-level trick, unrelated to
  performance.

## 5. B570 / B580 / B770

No B-series-only registry keys turned up in the sources. The driver stack is the same
(IGCL + Level Zero Sysman, same class-key names), so every knob above applies; the
differences are which features the driver enables (B580/B570 expose a voltage-limit
percentage and, on some OEM tools, VRAM overclocking; B770 depends on the shipped
driver). Treat "exclusive B-series tweak" lists as repackaged generic Windows tweaks.

## 6. Safe order of experiments on the A770

1. `HwSchMode` 2 ↔ 1 — measure frame time (PresentMon is already installed under
   `HKLM\SOFTWARE\Intel\PresentMon`).
2. MPO off (`Dwm\OverlayTestMode=5` or `Disable_OverlayDSQualityEnhancement=1`).
3. `Display1_DisableAsyncFlips=1` for tearing in borderless/windowed.
4. `Scheduler\HwQueuedRenderPacketGroupLimit` 2 (and `…PerNode` 1) for latency.
5. Do **not** touch `IGCLGamingDisableControl`, `FeatureTestControl`,
   `DisplayFeatureControl*`, `PowerAcPolicy` — they break the IGCL surface or the display
   pipeline with no documented semantics.

Every step: log the old value, reboot if the key is boot-time, measure 1 % lows and
frame-time p99, revert on regression.

## Sources

- Local inventory: `reg query` of the adapter class key, `HKLM\SOFTWARE\Intel\*`,
  `C:\ProgramData\Intel\IGSDS` (this machine).
- `github.com/nohuto/regkit` → `assets/records/Intel-00XX.txt` (key inventory).
- `github.com/djdallmann/GamingPCSetup` → `CONTENT/RESEARCH/FINDINGS/registrykeys_displayadapter_class_…txt`.
- `github.com/shoober420/windows11-scripts` → `GPUTweaks.bat` (Scheduler queue values,
  overlay keys).
- `frozentweaks.com/tweaks/hardware-accelerated-gpu-scheduling` (HwSchMode 1/2).
- `github.com/nitrocaster/igfxtweak` (dithering via registers).
- `github.com/RedDot-3ND7355/MPO-GPU-FIX` (MPO workaround).
- Intel community thread "Intel arc regedit" — indexed, but the page itself requires an
  authenticated session (authorization timeout for anonymous fetches).

Not verified: any effect on frames per second. The research stopped at "which knobs
exist and what they claim to do"; none of the above has been measured on this card yet.
