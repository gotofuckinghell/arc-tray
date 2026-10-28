# Arc A770 driver notes

Everything here was measured on a Sparkle Arc A770 16 GB, driver 32.0.101.9150,
Windows 11 24H2 (build 26100), IGCL runtime from
`DriverStore\FileRepository\iigd_dch_d.inf_amd64_dac34f82ca5b66d5\IntelControlLib.dll`.

## Identity and capabilities

- `ctlInit` with `flags = CTL_INIT_FLAG_USE_LEVEL_ZERO`, `Version = 0`,
  `AppVersion = 0x00010001`, zero UID works on this stack. The driver still rejects the
  zero UID on some packages — the code retries with the registered ArcTool UID
  (`e8e10f95-1a70-4b27-9ccf-02010264e9c8`).
- `ctlDeviceAdapterProperties` Version 3 is accepted here (320 bytes). Older runtimes
  answer `0x40000009 ERROR_UNSUPPORTED_VERSION`, hence the 3→2→1 walk.
- Advertised OC ranges on this card: power 115…276 W (default 230), core offset
  0…+300 MHz, voltage offset 0…+288 mV, temperature 60…90 °C, VRAM speed limit
  unsupported.

## Power limit goes far beyond the advertised range

IGCL reports a 276 W maximum, but the card happily accepts more through Level Zero:

```
zesPowerGetLimits  -> sustained 375 W, burst 375 W, interval 20800 ms
zesPowerSetLimits  -> 398 W sustained + burst, read-back 398 W
```

So the working path for anything above ~276 W is Sysman, not IGCL. Two quirks:

- `zesDeviceGetCardPowerDomain` returns `0x78000003` on this loader; use
  `zesDeviceEnumPowerDomains` and probe each domain with `zesPowerGetLimits`.
- `zeInit` before `zesInit` breaks the standalone Sysman context on this loader — call
  `zesInit(0)` on its own.

## Fan: the fixed-speed API is refused

`ctlFanSetFixedSpeedMode` answers `0x4000000a ERROR_UNSUPPORTED_FEATURE`. The IGS/Acer
mechanism works:

```
ctlFanSetSpeedTableMode(fan, table)   table.numPoints = 2
  point[0] = { 20 °C, speed = N, units = 1 (PERCENT) }
  point[1] = {100 °C, speed = N, units = 1 }
```

Read-back via `ctlFanGetConfig` shows `mode = 2` (table) and the requested percentage.
Live RPM comes from `ctlFanGetState(fan, 0)` (the properties advertise RPM and report
`maxRPM = -1`, so a percentage cannot be derived from it). Measured: 60 % → 2612 rpm,
80 % → 2811 rpm.

## Temperature limit: 90 °C is the driver ceiling, 115 °C needs the 2023 runtime

The DriverStore runtime clamps `ctlOverclockTemperatureLimitSetV2` to the advertised
max (90 °C; the request is refused with `0x44000005 ERROR_OC_OUT_OF_RANGE`). The
bundled 2023 `IntelControlLib.dll` exposes the V1 setters and accepts up to 115 °C:

```
ctlOverclockTemperatureLimitSet(dev, 115.0) -> SUCCESS
read-back through the modern runtime        -> 115 °C
```

That runtime answers `ctlGetDeviceProperties` with `ERROR_UNSUPPORTED_VERSION` for any
struct version, so device selection falls back to the first enumerated handle (IGCL
only lists Intel adapters). Its own `ctlOverclockTemperatureLimitGet` returns `-1` —
verification must go through the modern runtime.

## Voltage offset: positive only, and clearing resets everything

- Positive offsets work through IGCL (`ctlOverclockGpuMaxVoltageOffsetSetV2`,
  0…+288 mV). +50 mV reads back as +51 mV.
- **Setting the offset to 0 resets every IGCL overclock control**: the power limit
  dropped from 398 W to the 230 W default and the core offset from +75 MHz to 0. The
  app snapshots both values and re-applies them after a clear.
- Negative (undervolt) needs the Sysman V/F pair
  (`zesFrequencyOcSetMode(2)` + `zesFrequencyOcSetFrequencyTarget` +
  `zesFrequencyOcSetVoltageTarget`). Writing it rewrites the operating point instead
  of adding an offset: after the write the IGCL core offset read back as **+224 MHz**
  (asked +75) and a phantom **−31 mV** appeared. The path is therefore disabled — the
  code refuses negative values with an explanatory message.

## Core offset read-back drifts

Repeated reads of `ctlOverclockGpuFrequencyOffsetGetV2` after setting +75 MHz return
75 → 74 → 72 → 69 → 67 over a few minutes (the driver derives the offset against a
moving base). Menu checkmarks therefore match the *nearest* step of the grid
(±12 MHz) instead of an exact value.

## Measuring real board power

IGCL has no watts field. `totalCardEnergyCounter` (and `gpuEnergyCounter`) is a
**double in joules** — decoding the union as u64 yields garbage, as double it gives
sane numbers. Power = delta / delta_t:

```
idle (600 MHz, fan 0)                       39.5 W
llama-bench SYCL, MiniCPM5-2B, -p 4096       277…282 W, 2532 MHz, 71 °C
  telemetry flags during the load:           gpuPowerLimited + gpuCurrentLimited
```

`gpuPowerPercent` reports 0 (unsupported) on this driver.

Consequences:

- FurMark and Superposition only reach 160–180 W here — they do not saturate Xe, so
  they are useless as power-limit tests on this card. A compute load (llama.cpp SYCL,
  hashcat, OCCT "Power", 3DMark Speed Way / Steel Nomad stress) does.
- The card stops at ~280 W with the **current** limit flag, not the power-limit flag,
  even with a 398 W request. The connector/VRM current limit binds first (2×8-pin +
  slot = 375 W on paper), so anything above ~300 W is a register value, not a target.

## Sysman capability block (this card)

```
zesFrequencyOcGetCapabilities:
  maxFactoryDefaultFrequency 2400 MHz
  maxFactoryDefaultVoltage    832 (native mV)
  maxOcFrequency             4250 MHz
  minOcVoltageOffset 0, maxOcVoltageOffset 0, maxOcVoltage 0
```

After an IGCL voltage clear the driver reports no voltage/frequency target at all
(`0.0`); the code then falls back to the factory defaults above when it needs a
target for the (disabled) negative path.
