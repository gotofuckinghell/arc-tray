# AMD VSR — how it works (reverse-engineered from the installed driver) and what can be borrowed

Target: `amdkmdag.sys` (Radeon RX 580 2048SP, driver 31.0.21925.1001) from
`DriverStore\FileRepository\u0201039.inf_amd64_89c3272f19eab06b\B025980`, plus the driver INF.
Method: string/feature-name extraction (the KMD keeps its DAL feature names in plain text),
cross-checked against the adapter class key on a live system.

## Three similarly named things

| Name | What it really does | Direction |
|---|---|---|
| **VSR** — Virtual Super Resolution | The driver exposes display modes **above the panel's native resolution**; the GPU renders at that resolution and the display controller **downscales** it to the panel timing | render ↑, scanout = native |
| **RSR** — Radeon Super Resolution | Driver-side **upscaling** of a game rendered *below* native (FSR1 in the driver); exposed through ADLX as `IADLX3DRadeonSuperResolution` | render ↓, upscale |
| **FSR 1/2/3** | Game-side upscaling, no driver involvement | render ↓ |

The user-level effect of VSR is exactly what was described: pick 1440p/4K on a 1080p screen,
every game and the desktop render at that size, and the picture is downsampled — supersampling
anti-aliasing and a sharper image at the cost of GPU time.

## How the AMD driver implements VSR

All names below are literal strings inside `amdkmdag.sys` / the INF, i.e. this is the driver's
own feature surface, not a guess:

**1. Virtual modes and virtual EDID (display abstraction layer, "DAL")**

```
DalDefaultVirtualEDID          — fallback EDID used to build the virtual mode list
DalPreferDefaultEdidToVirtual  — which EDID wins when both are present
DalMaxSupportedVirtualDisplays — how many virtual targets are allowed
DalVirtualizedRefreshRateSupport — virtual modes also carry refresh rates
```

So VSR is *mode virtualization*: DAL adds timings that no real panel reports, Windows sees
them as ordinary display modes, and the user simply selects one.

**2. Downscaling in the display controller**

```
DalHighResDownScaleSupport     — the display block can downscale high-resolution sources
DalOverrideDownscaleLimit      — bypasses the per-ASIC downscale ratio limit
DALDisableDownscalingKickers   — disables "kickers" that veto downscaling
DalForceReduceLBForDownscaling — forces the line-buffer configuration downscale needs
DALLbDownscalingWa             — ASIC workaround for line-buffer downscaling
DalScalerBoundaryOption        — how the scaler handles edges when downscaling
```

That is the core: the scanout engine renders a bigger source than the output timing and the
line-buffer scaler compresses it. Without that hardware path there is no VSR.

**3. Variants and policy**

```
DalEnableVsr3, DalVsr3Multiplier   — "VSR 3" with an integer multiplier
DalVSR21x9                         — 21:9 aspect variant
DalVSRInitiallyDefaultToOff        — off until the user enables it
DalAllowAutoVSROnEmbeddedDisplay=1 — laptops may auto-enable VSR on the internal panel
DalDisableVSR                      — hard off switch
```

**4. Interaction with the rest of the pipeline**

```
"MPO disabled when VSR is on and not in fullscreen"
[bw_fixed] vsr[%d]:%d
[bw_fixed] vsr_after_rotation / vsr_after_stereo / vsr_after_surface_type
```

VSR forces the multi-plane-overlay path off while windowed (the downscaler owns the plane),
and the bandwidth accounting tracks the virtual surface through rotation/stereo/surface type.

**Where the knobs live.** Adapter class key
`HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\<NNNN>`.
On this machine (AMD adapter = `0001`) the INF sets
`DalAllowAutoVSROnEmbeddedDisplay = 1`, and no `DalDisableVSR` value is present — VSR is
available by default.

## Is there open-source code for this on Windows?

| Project | License | What it does | Fit for us |
|---|---|---|---|
| **ToastyX Pixel Clock Patcher** (vendored in `radeontray/vendor/reference/pixel-clock-patcher`) | zlib-style (use/alter/redistribute, keep notice) | binary-patches `amdkmdag.sys` to lift pixel-clock limits so custom high-res/high-refresh modes can be created | closest existing code: mode **unlock**, not downscale; assumes the driver can drive the timing |
| **CRU (Custom Resolution Utility)** | closed | writes EDID/mode overrides for high-res modes | technique reference only |
| **Linux `gamescope`** | permissive (MIT-style, part of Valve's stack) | renders a game at a higher internal resolution and scales to the output — software supersampling | exact *effect*, wrong OS and wrong layer |
| **Virtual Display Driver / IddSampleDriver** | MIT | adds a virtual monitor with arbitrary modes (Indirect Display Driver) | gives virtual modes, but nothing downscales to the real panel |
| **SpecialK** | source-available, custom licence | D3D hook that changes the game's render resolution | injection, per-game, not a display feature |

**Conclusion:** there is no open-source Windows VSR clone, because VSR is not an application
feature — it needs (a) mode virtualization and (b) a display-controller downscaler. The only
open code that touches (a) is the pixel-clock patcher style of driver patching.

## What this means for the A770

Intel's IGCL has no VSR API. The only related surface is
`ctlGetSetCustomMode`, whose structure carries just `SourceX`/`SourceY` (custom **source**
sizes, no timings), plus `ctlSetCurrentScaling` / `ctlGetSetRetroScaling`. Whether Intel's
display engine can downscale a source **larger** than the panel is exactly the open question.

**Probe plan (read-only first):**

1. `ctlEnumerateDisplayOutputs` → handle for the monitor.
2. `ctlGetSupportedScalingCapability` / `ctlGetCurrentScaling` → what scaling modes and limits
   the engine reports.
3. `ctlGetSetCustomMode(GET_CUSTOM_SOURCE_MODES)` → existing custom source modes.
4. Only then, with a throwaway mode and an automatic restore, try adding a source mode larger
   than the panel (e.g. 2560×1440 on a 1080p output) and see whether the driver accepts it and
   whether the scanout comes back with a downscaled image.

If step 4 is rejected (`ERROR_UNSUPPORTED_FEATURE` / invalid argument), Intel has no downscale
path and the feature is **blocked** on this hardware — same conclusion as F-003. If it is
accepted, ArcTray can expose "virtual resolution" slots without any driver modification, which
would be the real Intel analogue of VSR.
