# WSL Cleaner

A small, dependency-free .NET 10 console application for a **read-only Windows disk inventory**.
Despite the name, this increment does not clean anything. It prints one snapshot and exits;
it is not a resident monitor and does not prevent a disk from filling up.

## Build and run on Windows

Install the .NET 10 SDK. In PowerShell, from the repository root:

```powershell
dotnet build WslCleaner.slnx --configuration Release
dotnet run --project src/WslCleaner --configuration Release --no-build -- --help
dotnet run --project src/WslCleaner --configuration Release --no-build
dotnet run --project src/WslCleaner --configuration Release --no-build -- --json
```

No administrator privileges, WSL launch, Docker daemon connection, or NuGet package dependencies
are required. Run as the Windows user whose WSL registrations and Docker Desktop data you want
to inspect. Running from inside WSL uses Linux .NET and returns unsupported; use Windows .NET
from PowerShell instead. The .NET CLI itself may create normal build/SDK artifacts; the
inventory executable does not write files or settings.

Options are exactly `--help` and `--json`, individually. No arguments prints a text panel;
unknown, combined or repeated options are rejected. Exit codes:

- `0`: help, or a completed Windows snapshot (not proof that all Docker storage was found).
- `1`: unsupported platform, or a partial snapshot due to inaccessible/unsupported registered
  paths or failed measurements. Read the notes and per-item status.
- `2`: invalid arguments; diagnostic on stderr, no inventory collected.

On non-Windows, text mode writes `Windows only; no inventory collected.` to stderr.
`--json` instead emits a JSON report on stdout, with status `unsupported` and empty lists.
Help works on any platform. JSON uses camelCase keys: `status`, `message`, `volumes`, `vhdx`,
`notes`. Each volume has `path`, nullable `totalBytes` and `freeBytes`, and nullable `error`.
Each VHDX has `path`, `kind`, `labels`, nullable `lengthBytes`, `status`, and nullable `error`.
Null means unknown, never zero. VHDX status is `measured`, `missing`, or `unavailable`.
Reports may contain local paths and distribution names; review them before sharing.

## What is measured

- Fixed Windows volumes: total bytes and total free bytes, not user-quota available space.
- Current-user WSL registrations: read-only `HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss`.
  Version 2 uses `BasePath` plus `VhdFileName` when present, otherwise `ext4.vhdx`.
  WSL1 or unknown versions are reported as unmeasured notes. Distros are never started.
- Docker Desktop: WSL registrations named `docker-desktop` / `docker-desktop-data` are
  labeled Docker, plus these bounded default-location candidates under `%LOCALAPPDATA%`:
  - `Docker\wsl\disk\docker_data.vhdx` (modern data)
  - `Docker\wsl\data\ext4.vhdx` (legacy data)
  - `Docker\wsl\distro\ext4.vhdx` (legacy engine)

Missing default candidates are shown explicitly and do not themselves make a snapshot partial.
They do not establish whether Docker is installed. Custom/moved Docker storage not exposed by
those WSL registrations, Hyper-V storage, other Windows users, and remote engines are
**unsupported/unknown**. There is no recursive scan or Docker settings parser. Only absolute
fixed-drive paths are accepted; UNC/network paths are unsupported.

**VHDX file length is not physical allocated bytes**, guest-used bytes, maximum virtual disk
capacity, container/image usage, or reclaimable space. Sparse files can have very different
physical allocation. Values are exact byte counts for the measurement made, not estimates
of those other quantities. There is intentionally no summed WSL/Docker storage total.
Paths are normalized and compared case-insensitively so a disk found through WSL and a Docker
candidate appears once with both labels. Hard-link/junction/mount-point aliases are not resolved.
VHDX bytes already reside on a host volume: adding them to used volume space would double-count.
Volumes and files can change during collection; this is a non-atomic snapshot.

## Safety and limits

The application only reads volume information, registry values, and file metadata. It does
not execute external commands, connect to Docker, start services/distros, change permissions,
delete files, compact disks, or modify registry values. Access failures are reported, not
worked around with privilege escalation or mutation. No file contents are read.

Not implemented: configurable size limits, alerts, background/resident operation, tray/UI,
installer, cleanup, orphan/image identification, compaction, and automatic maintenance.
A future roadmap is limits and read-only alerts first, then separately designed, explicitly
approved maintenance with its own safety checks. None of that is a present capability.

## Checks and validation boundary

```powershell
dotnet build WslCleaner.slnx --configuration Release
dotnet run --project tests/WslCleaner.Checks --configuration Release --no-build
```

The dependency-free harness launches the real CLI and checks exit code, stdout and stderr
(normalizing Windows newlines). It covers help, invalid arguments, and an invalid JSON
combination on every host; non-Windows also checks both unsupported output modes. These
five checks first failed against a deliberate unimplemented entry point before production
implementation. The unsupported-platform checks explicitly skip on Windows.

There is no fixture-injection switch in the production CLI. Discovery and Windows text/JSON
formatting are not covered by synthetic host fixtures in this increment. A native Windows
smoke check passed on the development machine: three CLI checks passed (two unsupported-host
checks skipped), and JSON inventory returned exit 0 with fixed volumes, the registered Ubuntu
VHDX, the Docker Desktop data VHDX and engine VHDX. Build completed without warnings.
The first Windows help check exposed a console-codepage difference; ASCII headings fixed it.
Legacy/custom layouts, stopped-only environments, alias deduplication, access failures and
alternative registry states still need dedicated Windows fixtures. One successful snapshot
does not establish compatibility with every installation. Linux CLI checks alone do not
demonstrate native Windows discovery correctness.
