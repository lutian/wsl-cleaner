# Native Windows application

This frontend is C++17/Win32: no managed framework, browser, downloaded runtime, or
resident elevation broker. A system application icon is used in the notification
area. The fixed, black/monospace information panel uses native controls.

## Build integration

Compile `native/src/app.cpp` with the shared native core and maintenance sources,
include `native/include`, and use a Windows GUI subsystem with the Unicode
`wWinMain` entry point. C++17 and native thread support are required. Native MSVC
Release build, read-only control smoke, and isolated install/uninstall checks
passed. No production WSL/Docker cleanup or shutdown was executed during testing.

Required Windows import libraries:

```text
user32 shell32 comctl32 ole32 advapi32 comdlg32 gdi32
```

For a GNU-compatible Windows toolchain, the frontend flags are:

```text
-municode -mwindows -luser32 -lshell32 -lcomctl32 -lole32 -ladvapi32 -lcomdlg32 -lgdi32
```

`gdi32` is additionally required by native fonts, brushes and text colors.

Link the toolchain's C++ runtime statically if the distribution promises a single
native executable; verify actual imports on the produced binary. Do not ship a
compiler/runtime installer. Core adapters may require additional import libraries;
these flags describe the frontend only. Name the packaged executable
`WslCleaner.exe`.

## Monitoring and settings

The first inventory and each subsequent poll run on a background worker, not the
message loop. Only one operation (inventory, planning, automatic cleanup or manual
maintenance) runs at a time; timer ticks while busy are skipped, not queued.

- Host capacity/free bytes, WSL and Docker VHDX metadata are supplied by the shared
  inventory core. **Only VHDX file length is measured**: physical allocation,
  reclaimable bytes, guest filesystem use and filesystem maximum are unknown.
  A large file does not prove its entire length consumes physical storage.
- Resource budgets are advisory thresholds, **not hard quotas**. Select a disk,
  enter its budget in bytes, and Save. Repeat for other disks. `0` removes that
  disk's budget. Polling refreshes the selection list; save edits before the next
  refresh. A currently undiscovered disk's saved budget is retained.
- Reserve is host free-space reserve in bytes. Warning/critical are percentages;
  poll and cooldown are seconds; minimum age is days. Shared `ValidateConfig`
  supplies authoritative range errors (poll 10–86400, warning < critical, 1–100,
  cooldown 0–604800, age 0–36500). Save validates the full candidate before calling
  shared `SaveConfig`; invalid input is not persisted.
- The frontend store is `settings.conf` under shared `DataDirectory()`. No write is
  made merely by starting or polling. Missing/corrupt/unreadable configuration is
  visibly reported; automation and manual maintenance are disabled until the user
  explicitly saves a valid configuration. A failed load does not quietly overwrite
  the original file. Explicit Save may replace an invalid store, so preserve a copy
  first if diagnosis/recovery is needed.
- All destructive opt-ins default off. Enable automatic Docker cleanup separately
  from the build-cache, dangling-image, and **labeled disposable-container only**
  categories. Minimum age applies through the shared maintenance policy. Ordinary
  stopped containers and volumes are never a cleanup category.

`AlertEngine::Evaluate` is the sole warning/escalation/cooldown authority. An alert
shows a Windows notification and also opens the panel (including when Windows
suppresses notification balloons). Clicking the notification or tray icon opens
the panel. Right-click the tray for Show/Exit. Closing the window hides it to the
tray; if tray creation failed, it stays accessible instead of disappearing.

The resident process uses a user-SID-qualified global mutex: a second instance
for that user exits with a notice, including across interactive sessions. It does
not send commands to or replace an existing instance. If mutex creation fails it
fails closed. Startup is opt-in through the installer, never implicitly enabled
by saving configuration.

## Maintenance consent and lifetime

Manual buttons **prepare**, rather than immediately execute:

1. Save valid settings, choose enabled categories/age, and select the relevant
   discovered disk for WSL cache or compaction. Docker cleanup always prepares
   shared `Action::DockerCleanup` with target `docker`; WSL apt-cache cleanup uses
   `Action::WslAptCache` with the selected registered distribution name, not the
   disk budget ID or VHDX path.
2. Inspect the **full scrollable plan**, target, expiration, steps and backup
   folder. Deletion is irreversible. Type the exact uppercase `CLEAN`, then
   Execute. A plan can expire; prepare a new one after any error or refresh.
3. For offline compaction, first explicitly exit the non-elevated resident app,
   then run it as administrator yourself. The app checks administrator membership
   before planning and shows instructions on failure; it never elevates itself.
   Choose an existing backup directory with the folder chooser, review the full
   plan and its backup/identity/exclusive-access prerequisites, type exact
   uppercase `COMPACT`, and separately tick **AllowShutdown**. All WSL
   distributions and Docker workloads will be stopped, not just the selected
   disk. Consistent verified backup and shared service checks remain mandatory.
   Neither a typed word nor an administrator token bypasses those service checks.

Only Docker cleanup may run automatically, and only for a Docker disk budget
alert, with automatic cleanup enabled and at least one category explicitly opted
in. It uses the same Prepare/Execute service and configured category/age policy.
There is at most one attempt per configured cooldown, with a minimum 60-second
attempt delay even if cooldown is zero. The attempt is recorded before preparation,
so failures/exceptions also incur the delay. Host reserve alerts alone do not
trigger Docker cleanup. WSL cache cleanup and compaction are **never automatic**.

Exit during a worker operation keeps the window/message pump alive, disables new
actions and waits for the worker completion message before destroying UI state.
Workers are never detached; a guarded result slot transfers ownership to the UI,
notified through `WM_APP`. The live timer drains the same slot if message posting
fails, including while Exit is waiting. The shared service has no cancellation API: exit does not kill a backup,
cleanup, or compact operation midway. Normal interactive session shutdown is
refused while work is active; forced OS termination/power loss cannot be made
safe by the UI. Background exceptions and service failures are shown as errors.

## Public command-line interface

```text
WslCleaner.exe --help
WslCleaner.exe --version
WslCleaner.exe --inventory
WslCleaner.exe --smoke-test
WslCleaner.exe --tray
```

Use `Start-Process -Wait -PassThru` or a process API when waiting on this GUI
executable. Output uses inherited redirected stdout or an explicitly attached
parent console; no console is allocated automatically. Exit status is 0 on success,
1 on initialization/inventory failure, 2 for unsupported or multiple arguments.
Diagnostics for these modes use stdout; stderr is empty. `--version` prints
`WSL Cleaner native 1`. `--help` describes these public flags.

- `--inventory` prints read-only inventory with default advisory settings. It does
  not load/save settings, create a tray icon, poll, or perform maintenance. It is
  a **real host read**, so do not use it as an isolated development smoke test.
- `--smoke-test` initializes COM and native controls and exits immediately with
  `SMOKE OK: native controls initialized; no tray, polling or maintenance.` It
  skips the resident mutex, configuration/data directory, inventory, timers and
  tray. It is a public control-initialization test, **not** a consent/privilege
  bypass, and does not prove rendering/notification behavior or maintenance.
- `--tray` starts ordinary resident monitoring, initially hidden only when a tray
  icon exists and settings loaded successfully. It does not suppress errors or
  authorize cleanup. No arguments starts with the panel visible.

There is no test flag that injects fake approval, disables identity/backup checks,
or executes maintenance without normal consent.

## Per-user native-only installation

Distribute `WslCleaner.exe` alongside `Install.ps1`, `Uninstall.ps1` and
`Packaging.Common.ps1`. Inspect/trust the package before running scripts. No
signature/authenticity claim is made by the scripts.

```powershell
# Does not start the app, elevate, or install dependencies:
.\Install.ps1 -ExecutablePath .\WslCleaner.exe
# Optional per-user Startup shortcut (runs --tray at next logon):
.\Install.ps1 -ExecutablePath .\WslCleaner.exe -EnableStartup
# After explicitly exiting the app:
& "$env:LOCALAPPDATA\Programs\WslCleanerNative\Uninstall.ps1"
```

Installation is fixed to `%LOCALAPPDATA%\Programs\WslCleanerNative`. **Any existing
destination is refused**, even an empty one: there is no overwrite/upgrade mode.
The manifest owns only `WslCleaner.exe`, `Uninstall.ps1`,
`Packaging.Common.ps1`, and `install-manifest.json`. No settings/backups appear in
this list; keep backups outside the installation directory. No app is launched
on install, and neither script kills any process. Uninstall verifies the fixed
manifest, rejects incomplete/foreign manifests, and checks exclusive executable
access before deleting individual literal owned paths. It removes its startup
shortcut only when enabled by the manifest and its target/arguments still match.
Unknown files are retained and prevent removal of the installation directory.
There is no recursive, wildcard, forced, registry-wide or profile-wide deletion.

Both scripts inspect package/helper and destination ancestors for reparse points;
UNC installation paths and existing startup shortcuts are refused. Settings under
`DataDirectory()` and user-selected backups are untouched. Guards do not provide
an adversarial concurrent-filesystem transaction: run from a trusted local package
with no other installer/file manipulator operating on these directories. If an
install/uninstall fails midway (permissions, running image, antivirus or disk
errors), stop and inspect the reported paths; do not broadly delete directories.
A failed partial install is not automatically rolled back or overwritten.

## Verification status and safe checks

The parent built the final source snapshot with MSVC 19.44 and observed Windows
CTest 3/3, shell/API checks 16/16 and isolated install/uninstall checks 4/4 passing.
Public help, version, rejected arguments and native-control initialization were
exercised. The installer used a disposable temporary profile, did not launch the
resident app or enable startup, and preserved unrelated files, settings and backups.
The portable core and maintenance suites passed 18/18 and 19/19 groups respectively.
Independent static verification found no demonstrated severe candidate-caused blocker.

There is no meaningful automated visual UI RED/GREEN claim. Interactive rendering,
resident monitoring/notifications and adversarial installer races remain unverified.
No production cleanup, shutdown or compaction was executed on working WSL/Docker data.

On Windows, parser/structural checks (no installation and no executable launch):

```powershell
powershell.exe -NoProfile -File native\tests\shell_checks.ps1
```

Public non-maintenance executable tests, only on an approved Windows test host:

```powershell
powershell.exe -NoProfile -File native\tests\shell_checks.ps1 -Executable C:\test-package\WslCleaner.exe
```

The latter runs only help, version, unknown-argument rejection and control smoke,
using redirected output and checking exit codes. It does not exercise resident
monitoring or mutate installer state. A timed-out child is reported, not killed;
inspect that specific process manually. Script checks parse all packaging scripts
and inspect fixed ownership, guards and UI API/consent wiring. Structural checks
are not proof of filesystem race resistance, backup correctness, or UI behavior.

Parent Windows smoke still needs to check black/monospace readability, full plan
scrolling, DPI/keyboard usability, tray show/hide/alerts, per-user instance policy,
corrupt settings visibility and explicit Save, responsiveness under slow worker
operations, and safe Exit. Installer runtime tests belong in a disposable user
profile/VM, including collision/reparse/unknown-file preservation, startup opt-in
and settings/backup preservation. Dangerous workflows require isolated fake or
disposable state; never exercise them on the developer's working WSL/Docker data.
