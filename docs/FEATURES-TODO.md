# Feature backlog (recorded, not implemented yet)

Status legend: **TODO** (agreed, not started), **PROBE** (needs one measurement first),
**BLOCKED** (driver/hardware refuses), **DONE** (shipped).

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
