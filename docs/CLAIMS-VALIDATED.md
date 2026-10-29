# Validation of planned / unimplemented claims

Method: the claims were written down as a document, together with the raw transcripts
(`probe_vram`, `probe_features`, KMD string counts, PnP state, Ghidra decompilation, export
diffs, power measurements, external references) and the official `igcl_api.h`. An
independent reviewer (OpenCode, `--agent plan`, read-only) had to classify each claim as
SUPPORTED / CONTRADICTED / UNVERIFIABLE with file:line citations, and flag logical errors.

## Result

| # | Claim | Verdict | Correction |
|---|---|---|---|
| 1 | VRAM speed limit cannot be patched into life | **SUPPORTED** (with a causality caveat) | `supported=0` + `ERROR_UNSUPPORTED_FEATURE` + no `VramMem`/`MemPll` strings in four KMDs. "No code at all" is still an inference from string absence, not proof |
| 2 | Custom V/F curve unavailable on DG2 | Observations **SUPPORTED**, "source absent" **CONTRADICTED** | `ERROR_DATA_READ` ≠ `UNSUPPORTED_FEATURE`: the call exists and reading fails. Say "data cannot be obtained", not "no data source" |
| 3 | PMT is read-only and cannot control throttling | **SUPPORTED** for the control part | KMD has discovery/shared-memory symbols only, no PMT write path |
| 4 | PL4 has no software path | **UNVERIFIABLE**; "no PL4 strings" **CONTRADICTED** | A lone `KPL4` token exists in 9150. Right wording: no PL4 *control* interface was found; whether the PCU mailbox holds an OCP threshold was never checked |
| 5 | PL1/PL2/tau "mailbox" is the only power lever | **CONTRADICTED** as written | `src/arc.c` writes through `ctlOverclockPowerLimitSetV2` (IGCL) and `zesPowerSetLimits` (Sysman); "mailbox" never appears in the code. And voltage/clock also move power, so "only lever" is wrong |
| 6 | Negative offset reachable through the 5333 ControlLib | **UNVERIFIABLE** (declared as an experiment) | Only the export diff exists; the 5333 library was never run against this driver |
| 7 | Per-app settings unimplemented | **SUPPORTED** for "ArcTray writes global only"; **CONTRADICTED** as "id 15 proves the feature is absent" | `DATA_NOT_FOUND` means "no state stored", not "feature missing" |
| 8 | Display/colour exists in IGCL but not in ArcTray; no VSR analogue | **SUPPORTED**, except one symbol name | The setter is `ctlSetCurrentSharpness`, not `ctlSetSharpness`; docs corrected |
| 9 | Installing `Intel_NF_I2C.sys` alone may not be enough | **UNVERIFIABLE** | The KMD logs both "child driver is disabled" and "not supported on this platform"; the docs elsewhere claimed the transactions fail *because* the driver is missing — the two statements cannot both be certain |
| 10 | XeLL is latency, not an upscaler | **SUPPORTED** | `CTL_3D_FEATURE_LOW_LATENCY = 16` (OFF/ON/ON_BOOST) vs `CTL_VIDEO_PROCESSING_FEATURE_SUPER_RESOLUTION = 4` in the video pipeline |
| 11 | Feature ids 0,1,3,5,6,7,8,10,14,18,20 return `UNSUPPORTED_FEATURE` on 9150 | **SUPPORTED** | Direct probe transcript |

## Editing consequences applied

- `docs/ARC-CONTROLS-REFERENCE.md`: sharpness setter renamed to `ctlSetCurrentSharpness`;
  the 3D `SHARPENING_FILTER` entry now points out that it is a different control from the
  display sharpness API.
- `docs/DRIVER-VERSIONS-STATS.md`: V/F wording softened (`ERROR_DATA_READ` ≠ absent source),
  PL4 wording corrected (a lone `KPL4` token exists; absence of strings ≠ absence of code).

## Lessons that stay

1. "The API returned X" is evidence; "therefore the hardware cannot do Y" is an inference
   and must be labelled as such.
2. Absence of strings in a binary is weak evidence — always pair it with a runtime result.
3. Different error codes mean different things: `UNSUPPORTED_FEATURE` (no feature) vs
   `DATA_NOT_FOUND` (no state stored) vs `DATA_READ` (read attempted, failed).
4. Do not name internal mechanisms (mailbox, PCU) unless they appear in the code or in a
   primary document describing this exact driver stack.
