# Proposed features and their independent validation

Proposals were written down as a spec, then handed to OpenCode (`--agent plan`, read-only)
together with the raw evidence (live probe transcripts, export lists, KMD strings, PnP
state) and the official `igcl_api.h`. The reviewer had to classify every item as READY /
NEEDS-PROBE / INFEASIBLE, with file:line citations, risk and effort.

| ID | Feature | Verdict | Risk | Effort | Missing evidence |
|---|---|---|---|---|---|
| P1 | Multi-point fan curve (`ctlFanSetSpeedTableMode`, up to 32 temp→speed points) | **READY** for N=2, probe needed for N>2 | low-med | M | readback with more than two points |
| P2 | Fan presets (quiet/balanced/aggressive) on top of P1 | NEEDS-PROBE | low | S | a working multi-point profile |
| P3 | Graphics presets (competitive/quality/balanced) | READY | low | S | — (but close to `--gfx-save/restore`) |
| P4 | `--state --json` machine-readable dump | READY | low | S | — |
| P5 | Telemetry CSV logger (watts from the energy counter) | READY | low | S | — |
| P6 | Reset/TDR watchdog that re-applies the last good point | NEEDS-PROBE | high | M | which result codes actually appear after a TDR on 9150 |
| P7 | Tweak bundles with rollback | READY | low | S | — (duplicates `--tweak` plumbing) |
| P8 | Display panel: colour range, BPC, scaling mode (`ctlGetSetDisplaySettings`) | NEEDS-PROBE | med-high | M-L | `ctlEnumerateDisplayOutputs` + `ControllableFlags` on this monitor |
| P9 | Display sharpness slider (`ctlGetSharpnessCaps` / `ctlSetCurrentSharpness`) | NEEDS-PROBE | medium | M | the capability answer on this display |
| P10 | Per-application settings (feature 15 with `PER_APP`) | NEEDS-PROBE | medium | M | a write of id 15 = PER_APP plus readback |
| P11 | Undervolt through the 5333 ControlLib | NEEDS-PROBE | high | L | run `ctlOverclockGpuVoltageOffsetSetV2(<0)` against KMD 9150 |
| P12 | Global hotkey to cycle presets / toggle FPS cap | READY | low | S | — |
| P13 | Live power/temperature in the tray tooltip | READY (design caveat) | low | S | must not fight `release_hw()` after the 20 s idle timer |
| P14 | VRAM memory-speed and V/F-curve sliders | **INFEASIBLE** | high | L | driver returns `UNSUPPORTED_FEATURE` / `DATA_READ` |

## Reviewer notes that changed the design

- **P13** conflicts with the idle-release design: after 20 s the driver handle is closed, so
  a tooltip refresh cannot read live values without keeping the handle open (which would
  cost the 8.5 MB idle footprint). Options: refresh only while the menu was used recently,
  or cache the last sample and show it with a timestamp.
- **P4/P5** partially duplicate `--watch` and `--state`; their value is the machine-readable
  format, not new data.
- **P7** duplicates the existing `--tweak=` plumbing and "revert all"; value only as a
  single-click bundle.
- **P3** is close to `--gfx-save/--gfx-restore`; useful only as fixed, named presets.
- **P9** must not be confused with 3D feature id 7 (`SHARPENING_FILTER`), which this driver
  answers `UNSUPPORTED_FEATURE`; the display API is a different control path.

## Suggested order of work

1. P4 + P5 (small, no new API, immediately useful for measurements).
2. P1 (fan curve) with a probe for N>2, then P2 on top.
3. P12 + P13 (UI quality of life, with the idle-cache caveat).
4. P8/P9/P10 after one probe session each (display outputs, sharpness caps, scope write).
5. P6 only after a real TDR capture; P11 remains a lab experiment.
6. P14 is closed: the driver does not provide it (see `docs/CLAIMS-VALIDATED.md`).
