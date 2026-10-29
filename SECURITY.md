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

## Audit 2026-10-29 (post 1.7.1)

Scope: `src/tray.c`, `src/arc.c`, `src/gfx.c`, `src/tweaks.c`, build flags, deployment
ACLs, git history. Result: 4 fixes, 3 informational items, no network/IPC surface
(`WSAStartup|socket|WinHttp|InternetOpen|URLDownload` → 0 hits), no secrets in history
(`ghp_…|-style token patterns and `api_key`/`password` literals → 0 hits).

### F2 — unsigned third-party runtime loaded into an elevated process (fixed)

The bundled 2023 IGCL (`IntelControlLib-2023.dll`, needed for temperature limits above
90 °C) carries **no Authenticode signature** (`Get-AuthenticodeSignature` → `NotSigned`),
while the DriverStore copy is WHQL-signed by *Microsoft Windows Hardware Compatibility
Publisher*. A swapped file in the tool folder would become code execution as
administrator.

Fix: the file's SHA-256 is pinned in `arc.c` (`EXT_DLL_SHA256 = B1D0FAD7…297EB`) and the
runtime is loaded only when the hash matches; `ARCTRAY_ALLOW_UNSIGNED=1` is the explicit,
documented escape hatch. The refusal prints the actual hash to stderr.

### F3 — DLL search order on load (fixed)

`LoadLibraryA(path)` lets the loader resolve the DLL's own imports through the current
directory and `PATH`. Both runtimes are now opened with
`LoadLibraryExA(..., LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)`.

### F4 — unqualified helper process name (fixed)

`ShellExecuteA("open", "notepad.exe", …)` resolved `notepad.exe` through the search
order. It now uses the absolute path from `GetSystemDirectory`. `schtasks.exe` was
already resolved against `%SystemRoot%\System32` inside `run_wait`.

### F5 — hand-edited profile could request out-of-range operating points (fixed)

Profiles live in a user-writable INI; values are now clamped to the advertised ranges
(PL 115…500 W, core 0…300 MHz, voltage 0…288 mV, temperature 60…115 °C, fan 30…100 %)
and every clamp is written to the log. The kernel driver still validates, this is
defence in depth against a typo turning into a multi-hundred-watt request.

### F6 — writes through reparse points (fixed)

The `%APPDATA%\ArcTray\` folder and the snapshot/log files are checked for
`FILE_ATTRIBUTE_REPARSE_POINT` before any write; a junction or symlink is refused.

### F7 — binary hardening (informational)

`DllCharacteristics = 0x0160` → `DYNAMIC_BASE | NX_COMPAT | HIGH_ENTROPY_VA` (ASLR, DEP,
64-bit ASLR). This mingw-w64 gcc emits **no** stack canaries: `-fstack-protector`,
`-strong` and `-all` all produce zero `__stack_chk_*` references (verified with a
one-function test), and CFG (`/guard:cf`) is MSVC-only. Residual mitigation is the small
codebase, static linking and the absence of parsers other than the INI reader.

### F8 — deployment ACL (verified)

`icacls` on the portable folder: `SYSTEM`, `BUILTIN\Administrators`,
`Administrator` — full control for those only. A non-admin cannot replace
`arctray.exe`/the DLLs in place. Moving the folder somewhere user-writable would void
this property; a public installer should target `Program Files`.

### F9 — accepted by design

- The tray keeps one IGCL client open for 20 s after a menu action, and the watcher keeps
  one open for its whole lifetime (~23 MB). No IPC, sockets or shared memory are added.
- `ARCTRAY_V1`, `ARCTRAY_NO_SYSMAN`, `ARCTRAY_PROPS_VERSION`, `ARCTRAY_VOLT_SYSMAN`,
  `ARCTRAY_TWEAK_DEBUG` remain honoured only for the un-elevated developer workflow; they
  change behaviour, not the loaded binary.
- Overclocking can still hang the GPU (TDR); that is availability, not a security boundary.

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
