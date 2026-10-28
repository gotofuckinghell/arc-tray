# Security policy and threat model

ArcTray is a local Win32 tool. It has no network code, no IPC, no service and no
file parsers beyond a small INI reader — so the attack surface is:

1. **The elevated context.** The tool must run as administrator, and the optional
   logon task runs it with `Highest Available` rights. Anything that can influence
   which binary or DLL runs in that context is a privilege-escalation bug.
2. **Local state** — the INI next to the exe, the log file, the scheduled task.
3. **The GPU driver boundary** — values written through IGCL / Level Zero are trusted
   input to the kernel-mode driver; this tool only writes documented ranges.

## Reporting

Open a GitHub security advisory or a private issue with the output of
`arctray.exe --selftest` and a description of the steps. Please do not post working
exploits in a public issue before a fix is available.

## Findings

### F1 — DLL path taken from the environment in an elevated process (fixed)

`ARCTRAY_IGCL` and `ARCTRAY_IGCL2023` let a caller point the loader at an arbitrary
`IntelControlLib*.dll`. With the logon task running elevated, any user who can set
their own environment variables could plant a DLL in a user-writable folder and get
code execution as administrator (CWE-426/427).

Fix: overrides are now ignored when the process is elevated (`is_elevated()` is
checked before reading the variable). The in-app search path (exe folder, DriverStore,
System32, Intel Graphics Software) remains.

### F2 — Elevated logon task pointing at a user-writable directory (mitigated by guidance)

`ArcTray Elevated` starts whatever `arctray.exe` exists at the recorded path with
administrator rights. If that path is writable by non-administrators (for example a
folder under `Documents`), replacing the file yields code execution as administrator
at the next logon (CWE-732).

Mitigation: the README and the portable notes now require an administrator-only
folder (`C:\Program Files\ArcTray`), and the About/README text calls the risk out.
The code cannot fix the ACL of the folder it was started from; a future release may
refuse to create the task when the folder is writable by non-admins.

### F3 — Log file in `%TEMP%` followed reparse points (fixed)

The diagnostics log was appended to `%TEMP%\arctray.log` and, while elevated, could
be redirected with a junction/symlink to an arbitrary path — an arbitrary-append
(and, on rotation, arbitrary-delete) primitive with administrator rights (CWE-59).

Fix: the log now lives next to the exe (the same folder as the ini), reparse points
are refused, and rotation truncates the same file instead of deleting it.

### F4 — Out-of-bounds read in the runtime path list (fixed)

The extended-runtime path list was shortened from three entries to two while the
loop kept a hard-coded bound of three, so `GetFileAttributesA` could be called with
a garbage pointer (CWE-125). Latent during a refactor, fixed before release: both
loops use `sizeof fixed / sizeof fixed[0]` now. `_Static_assert` sizes and a bounds
review are part of the build.

### F5 — DriverStore wildcard picks the first matching package (accepted, low)

`DriverStore\FileRepository\*iigd*` is scanned and the first directory containing
`IntelControlLib.dll` wins. A stale package could be loaded. This does not cross a
privilege boundary — `DriverStore` is writable by administrators only, and an
administrator already has code execution. Improving the choice (highest file version
of the active driver package) is on the list, not a security fix.

### F6 — No Authenticode check on the optional 2023 runtime (accepted, low)

`IntelControlLib-2023.dll` is loaded from the exe folder without signature
verification. Combined with F2 the file could be replaced — but only by someone who
can already write the install folder, which (per F2 guidance) must be admin-only. A
future release may call `WinVerifyTrust` and refuse unsigned copies.

### F7 — Trusted local state (informational)

`arctray.ini` is parsed with `fgets(256)` + `strtok_s` + bounded `snprintf`; names are
never used as format strings; menu labels are capped. The INI cannot execute code —
only values inside the same ranges the CLI accepts (they are clamped again in
`arc_set_*`). The autostart switch always writes a fixed task name with a quoted
absolute path, so the INI cannot inject command-line arguments.

## Hardware risk

Overclocking, power-limit and temperature changes are applied to real hardware. The
tool clamps to driver-reported ranges and verifies read-back, but unstable settings
can still crash or damage a card. Everything is at your own risk.
