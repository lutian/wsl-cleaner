# WSL Cleaner

A small **native Windows application in C++/Win32**, designed to help prevent WSL2 and
Docker Desktop virtual disks from exhausting Windows storage. No managed or browser runtime
will be required by the installed application.

> **Status: native implementation in progress.** No executable or installer is available yet.
> The abandoned managed prototype and its build files, tests, scripts and documentation have
> been removed from the current tree. Historical commits have not been rewritten.

## Planned experience

- One lightweight executable with a tray icon and a simple text-like information window.
- Host disk capacity/free space and detected WSL2/Docker VHDX sizes.
- Configurable advisory budgets per resource and minimum host free-space reserve.
- Background checks and Windows alerts before thresholds are reached.
- Explicitly enabled conservative cleanup, with preview and operation history.
- Separately authorized offline compaction with interruption and backup requirements disclosed.
- Small per-user installation, with startup opt-in.

Budgets are alerts, not filesystem hard quotas. VHDX file length, physical allocation,
virtual capacity and guest-used space are different measurements and will be labeled clearly.
Deleting data inside a virtual disk does not necessarily return space to Windows immediately.

## Safety boundaries

Stopped containers and unreferenced volumes are not automatically disposable. Never delete
volumes, ordinary stopped containers, projects, databases or personal files automatically.
Maintenance is opt-in and restricted to eligible categories. Compaction must not silently stop
Docker/WSL, modify a live disk, delete backups, or promise guaranteed reclaimed space.
Development checks must not clean, shut down or compact the user's working environment.

## Development

Scope and progress: [`odd/tasks/native-windows-app.md`](odd/tasks/native-windows-app.md).
Verified native build/package instructions and test evidence will be added with the implementation.
There is no current native release, and no build or readiness claim is made for the unfinished app.
