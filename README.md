# WSL Cleaner

**Target: a small native Windows application in C++/Win32, with no .NET runtime requirement.**
It will help prevent WSL2 and Docker Desktop virtual disks from exhausting Windows storage.

> Status: native implementation is pending. The C# code currently under `src/WslCleaner`
> is the earlier read-only prototype, not the final application or an installable native release.
> There is no native executable or installer available yet.

## Planned Windows experience

- One lightweight application with a tray icon and a simple text-like information window.
- Host disk capacity/free space and detected WSL2/Docker VHDX file sizes.
- Configurable advisory budgets per resource and a minimum free-space reserve per host disk.
- Background checks and Windows alerts before configured thresholds are reached.
- Explicitly enabled conservative cleanup, with a preview and operation history.
- Separately authorized offline compaction, with workload interruption disclosed in advance.
- A native executable and a small per-user installer; no .NET, Electron or browser runtime.

Thresholds are alerts, not filesystem hard quotas. VHDX file length, physical allocation,
virtual capacity and guest-used space are different measurements and will be labeled as such.
Deleting files inside a virtual disk does not necessarily return space to Windows immediately.

## Safety boundaries

Ordinary stopped containers, Docker volumes, projects, databases and personal files are not
considered disposable. Automatic maintenance is opt-in and restricted to eligible categories.
Compaction must not silently stop Docker or WSL, compact a live disk, delete recovery points,
or claim guaranteed reclaimed space. Unsupported configurations must be reported explicitly.
Development verification must not clean, shut down or compact the user's working environment.

## Current prototype

The existing dependency-free C#/.NET 10 console prototype collects a single read-only snapshot
and exits. It supports `--help` and `--json`; it does not provide alerts, limits, cleanup,
compaction, a tray icon or an installer. Unlike the future native app, it requires .NET.
Its original usage and measurement details are preserved in [the prototype documentation](docs/prototype-dotnet.md).

## Development and verification

The native feature scope and implementation status are tracked in
[`odd/tasks/native-windows-app.md`](odd/tasks/native-windows-app.md).
Native build/package instructions will be added when their commands have been verified.
No unverified download or native installation instructions are advertised here.

The prototype passed its build and five Linux CLI checks, plus three Windows CLI checks
(two non-Windows cases skipped) and a real read-only Windows inventory smoke check.
Those results do **not** establish native application readiness. Broader discovery layouts,
access failures and maintenance workflows still need dedicated isolated verification.
