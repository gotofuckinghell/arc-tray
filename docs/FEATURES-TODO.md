# Feature backlog (recorded, not implemented yet)

Status legend: **TODO** (agreed, not started), **PROBE** (needs one measurement first),
**BLOCKED** (driver/hardware refuses), **DONE** (shipped).

## F-002 — Everything lives next to the exe (portable discipline) — TODO

**Rule.** All state the program itself creates belongs in the folder with `arctray.exe`:
INI, logs, snapshots, watch logs, backups. Nothing is scattered through `%APPDATA%`,
`%LOCALAPPDATA%`, `%TEMP%` or the registry *for the program's own data*. Copy the folder to
another machine and the whole state travels with it.

**Only two deliberate exceptions**

1. **Driver DLLs that may need swapping.** `IntelControlLib.dll` lives in the DriverStore and
   is not ours to move; the version-override story is "put a pinned `IntelControlLib-XXXX.dll`
   next to the exe and let the loader prefer it" (the 2023 runtime today, the 5333 runtime for
   the undervolt experiment tomorrow). Those files stay external and hash-pinned — the program
   never copies DLLs into the system.
2. **Registry tweaks.** They are registry values by definition (HAGS, MPO, MMCSS…). They stay
   opt-in, reversible, and never used to store application state.

**Current paths vs. the rule**

| What | Now | Should be |
|---|---|---|
| `arctray.ini` (profiles) | exe dir, `%APPDATA%\ArcTray\` fallback | exe dir; fallback only if the folder is read-only |
| `arctray.log` | exe dir | exe dir |
| `gfx-snapshot.ini` | `%APPDATA%\ArcTray\` | **exe dir** (`--gfx-save` default) |
| `gfx-watch.log` | `%APPDATA%\ArcTray\` | **exe dir** |
| tweak backups | not stored | exe dir, `tweaks-backup.ini`, written before the first apply |

**Work**

- Move the default snapshot/watch paths to the exe directory, keep `--gfx-save=<file>` as the
  explicit override, and fall back to `%APPDATA%\ArcTray` **only** when the exe folder is not
  writable (read-only install, `Program Files`).
- Print the resolved paths in `--state` (or `--paths`) so it is obvious where data went.
- One-time migration: if an `%APPDATA%\ArcTray\` file exists and the exe-dir file does not,
  move it on first start and log the move.
- Snapshot the previous tweak values into `tweaks-backup.ini` before writing, so a revert can
  restore the user's originals instead of the hard-coded defaults.
- Acceptance: after a fresh run, nothing new appears outside the exe folder except the two
  documented exceptions; `--paths` lists every file the program can touch.

## F-001 — Menu must stay open after toggling an item — TODO

**Problem.** The tray menu is a `TrackPopupMenu` popup. Every item selection (a checkmark,
a value, a submenu leaf) sends a command and Windows closes the whole menu. Changing three
settings costs three right-clicks.

**Wanted behaviour.** Selecting an item applies it and leaves the menu open, with the new
state visible (the ● marker moves). The menu closes only when

* the user clicks outside it, or
* the user clicks the tray icon again, or
* the user picks `Exit` (or presses Esc).

**Implementation options**

1. **Re-show loop with `TPM_RETURNCMD`** (smallest change).
   `TrackPopupMenu(..., TPM_RETURNCMD, ...)` already returns the command instead of posting
   `WM_COMMAND`. Instead of breaking after one command, apply it, rebuild the menu
   (`build_menu()` already re-reads live state) and show it again at the same screen point,
   until the return value is `0` (user dismissed) or the command is `Exit`.
   Caveats: the menu visibly re-opens (flicker); keyboard navigation/accelerators reset each
   cycle; `TPM_RETURNCMD` swallows the message loop, so the balloon/`show_result` must be
   queued *after* the final close or drawn as a separate popup, not as a tray balloon during
   the loop; `DestroyMenu` on every rebuild to avoid GDI handle growth.
2. **Custom popup window** (cleanest UX, more work).
   A `WS_POPUP` window with owner-drawn rows, `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`,
   closing on `WM_KILLFOCUS` / click outside / second tray click. Gives per-item hover,
   persistent state and no flicker, but re-implements keyboard navigation and the submenu
   model.

**Acceptance criteria**

- Toggle HAGS, MPO, DPST in one menu session without it closing; each row shows the new
  ON/OFF state immediately.
- Click outside the menu → it closes, nothing else changes.
- Second click on the tray icon → toggles the menu (open → close).
- Esc closes; `Exit` closes and quits.
- No handle leak: opening/closing the menu 100 times keeps the GDI/USER handle count flat
  (check with Task Manager's GDI objects column).

**Notes.** `build_menu()` is already idempotent and re-reads live values, so option 1 needs
no changes to the command handlers — only the popup loop in `wndproc` (`WM_CONTEXTMENU` /
`WM_RBUTTONUP`). Tray left-click currently shows a status balloon; keep that behaviour.

## Backlog carried over from the feature review

See `docs/FEATURES-PROPOSED.md` for the full table with evidence.

| ID | Feature | Status |
|---|---|---|
| P1 | Multi-point fan curve (`ctlFanSetSpeedTableMode`, >2 points needs a probe) | TODO |
| P2 | Fan presets on top of P1 | PROBE |
| P3 | Graphics presets (competitive/quality/balanced) | TODO |
| P4 | `--state --json` | TODO |
| P5 | Telemetry CSV logger | TODO |
| P6 | Reset/TDR watchdog | PROBE |
| P7 | Tweak bundles with rollback | TODO |
| P8 | Display panel (colour range, BPC, scaling) | PROBE |
| P9 | Display sharpness slider (`ctlSetCurrentSharpness`) | PROBE |
| P10 | Per-application settings (feature 15) | PROBE |
| P11 | Undervolt via the 5333 ControlLib | PROBE |
| P12 | Global hotkey for presets | TODO |
| P13 | Live power/temperature in the tray tooltip | TODO (idle-release caveat) |
| P14 | VRAM speed / V-F curve sliders | BLOCKED |
