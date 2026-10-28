# Changelog

## 1.2.0

- `--watch[=sec]`: real board power in watts from the IGCL energy counter (which
  turns out to be a double in joules, not a u64 counter). Used to verify power limits
  without external tools.
- docs: measured idle 39.5 W and 277…282 W under a llama.cpp SYCL load with the
  `POWER` + `CURRENT` limit flags; FurMark/Superposition are not valid power tests on
  this card, and ~280 W is the current-limited ceiling.

## 1.1.0

- Tray menu with profiles, power limit, core offset, voltage offset, temperature
  limit and fan control; every apply is verified by read-back.
- Fan control on the A770 through the flat 2-point speed table (the fixed-speed API
  returns `ERROR_UNSUPPORTED_FEATURE`).
- Temperature limit up to 115 °C through the optional 2023 IGCL runtime.
- Autostart as an elevated logon task, toggled from the tray.
- About / Links entries (attribution and project links).
- Portable layout: config next to the exe, DLLs next to the exe, no installation.
- Driver-branch robustness: IGCL discovery fallbacks, structure-version walk,
  V1 setters, IGCL power-limit fallback, V1 telemetry.
- Security: environment DLL overrides ignored when elevated, log moved out of
  `%TEMP%` and protected against reparse points, out-of-bounds fix in the runtime
  path list (see SECURITY.md).

## 1.0.0

- First working build: power limit through Level Zero Sysman, core offset and
  temperature limit through IGCL, tray icon with preset submenus, CLI mode.
