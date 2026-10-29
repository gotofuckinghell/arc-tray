# Intel Arc KMD across four driver versions — static statistics

Target: `igdkmdnd64.sys` (the kernel-mode display miniport driver, KMD) from the four
packages in `repos/intel-igs`. Method: PE headers (`objdump -h/-p`), `strings -a/-el`,
Authenticode subject, `.pdata` function estimate (`entries = size / 12` for x64 RUNTIME_FUNCTION).
No disassembly yet — this is the "what is inside, how big, what names survived" pass.

## Identity

| Version | File size | Version string | PE timestamp | .text | .rdata | Functions (est., .pdata/12) | Signature |
|---|---|---|---|---|---|---|---|
| 4314 | 49 853 952 | 31.0.101.4314 | 2023-04-19 | 0x40F96B (4.3 MB) | 0x26D7988 (40.7 MB) | ~16 576 | CN=Intel Corporation |
| 5333 | 54 555 088 | 31.0.101.5333 | 2024-02-21 | 0x49F96B (4.8 MB) | 0x2A2C054 (44.2 MB) | ~18 303 | CN=Intel Corporation |
| 6297 | 59 626 936 | 32.0.101.6297 | 2024-11-12 | 0x51E5AE (5.4 MB) | 0x2E5988C (48.6 MB) | ~19 627 | CN=Intel Corporation |
| 9150 | 32 669 568 | 32.0.101.9150 | 2026-10-09 | 0x5DA835 (6.1 MB) | 0x15661AC (22.4 MB) | ~24 034 | CN=Intel Corporation |

Trend: code keeps growing (+45 % functions from 4314 to 9150) while the image shrank by
45 % — the newest build packs `.rdata` much tighter (22.4 MB vs 48.6 MB) and adds a new
section `fothk` (absent in all three older builds; 4314/5333 instead carry `.trs_age`).

Imports are identical in all four: `ntoskrnl.exe`, `HAL.dll`, `ksecdd.sys` — a normal
miniport, no user-mode dependencies.

## String inventory

| Metric | 4314 | 5333 | 6297 | 9150 |
|---|---|---|---|---|
| strings total (ascii+utf16) | 163 093 | 222 526 | 274 679 | 328 488 |
| assert-like strings (`.c(N)`) | 88 | 136 | 161 | 93 |
| `MID: 0x…` ETW event descriptors | 0 | 0 | 0 | **276** |
| format strings (`%…`) | 380 | 608 | 812 | 1 092 |
| "escape" mentions | 15 | 15 | 15 | 27 |
| "throttl…" mentions | 63 | 87 | 99 | **0** |
| `PMT`/`igrdPmt` | 3 | 3 | 2 | 7 |
| `MEI`/`igrdMei` | 0 | 24 | 27 | 27 |

Two structural changes stand out: the newest KMD adds a full ETW event catalogue
(`FID_KMD_HW_QUEUE`, `HwQueueDestroyed`, GMM events …) and loses every "throttling" string
that older builds carried — power-limit logic was rewritten or renamed, not removed (the
card still throttles), which makes the PMT module the next reverse-engineering target.

## Feature-specific probes on the same string sets

| Marker | 4314 | 5333 | 6297 | 9150 | Conclusion |
|---|---|---|---|---|---|
| `VramMemSpeed` / `VramMem` | 0 | 0 | 0 | 0 | no VRAM-speed code path in any KMD |
| `MemPll`, `GDDR`, `MemoryTraining`, `MemSSC` | 0 | 0 | 0 | 0 | memory PHY/PLL programming not present in the miniport |
| `VfCurve`, `VF curve`, `Overclock`, `PowerLimit` | 0 | 0 | 0 | 0 | OC/VF wording lives in `IntelControlLib.dll`, not KMD |
| `PLL` (any case) | 1 | 1 | 0 | 2 | incidental identifiers only (`PLL3`), not a clock-programming module |
| `I2C` / `INTC_I2C` | 4 | 4 | 5 | **19** | I2C stack exists **only** in 9150, with asserts from `igrdI2c.c` |
| `HwQueued…` | 0 | 0 | 0 | 2 | new scheduler trace names in 9150 |
| `FeatureTestControl` | 1 | 1 | 1 | 2 | registry value is read by the KMD itself |

Source-path asserts recovered from 9150 (none of the older builds carry them):
`igrdDdi.c`, `igrdI2c.c`, `igrdMei.c`, `igrdPmt.c` — all under
`D:\qb\workspace\21461\source\gfx-driver\Source\miniport\igrd\source\`. 6297 exposes only
`igrdDdi.c`.

## What this means for our goals

1. **VRAM memory speed**: the API exists only up to `IntelControlLib.dll`; the KMD in
   every generation contains no memory-PLL/PHY/training code and no `VramMemSpeed` symbol.
   This is consistent with the live probe (`vramMemSpeedLimit supported=0`,
   `ERROR_UNSUPPORTED_FEATURE`): there is nothing to patch in the KMD for this feature.
2. **VF curve**: the KMD has no VF wording either; `ctlOverclockReadVFCurve` fails with
   `ERROR_DATA_READ`, i.e. the data source (firmware table) is absent on DG2, not a hidden
   switch.
3. **I2C**: 9150 is the first package whose KMD actually carries the I2C module
   (`igrdI2c.c`, `INTC_I2C`, "I2C load disabled", "I2C device cookie not discovered").
   That matches the missing `Intel_NF_I2C.sys` child driver — the KMD side is ready, the
   bus driver is not installed.
4. **Power throttling**: strings disappeared in 9150, so the logic moved into the PMT
   module; that is where a real PL4/OCP investigation would start.
5. **Dynamic work**: 9150 exposes an ETW event catalogue, so the next step can be
   trace-based (GPUView/WPR) instead of static guessing.

## Reproduce

```bash
strings -a -n4 igdkmdnd64.sys | wc -l
objdump -h igdkmdnd64.sys | awk '/\.pdata/{print strtonum("0x"$3)/12}'
```
