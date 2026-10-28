# Changelog

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
