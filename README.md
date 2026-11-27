# WSL Cleaner

A lightweight **native C++/Win32 Windows application** for monitoring WSL2, Docker Desktop
and host-disk space. The x64 executable is approximately **480 KiB** and imports only Windows
system DLLs: no managed runtime, browser engine, local server or runtime download is required.

## Install or run

1. Download [WslCleaner-native-v1.zip](packages/windows-x64/WslCleaner-native-v1.zip).
2. Extract it to a **local Windows folder**, not a WSL/network UNC path.
3. Double-click `WslCleaner.exe` for portable use, or `Install.cmd` for per-user installation.
4. Set budgets in the panel and explicitly save. Closing the window keeps the tray monitor
   running; use **Exit** to quit. Installation does not launch the app or enable startup.

For optional login startup, run `Install.cmd -EnableStartup` during the first installation.
The installer refuses existing destinations; it does not silently upgrade or overwrite.
To uninstall, run the installed `Uninstall.ps1` with Windows PowerShell. Settings, backups
and unknown files are preserved. Package checksums are in [SHA256SUMS.txt](packages/windows-x64/SHA256SUMS.txt).
The build is unsigned; Windows may show an unknown-publisher warning. Hashes verify integrity,
not publisher identity. Review the source and do not bypass organizational security policy.

## What it does

| Feature | Behavior |
|---|---|
| Inventory | Fixed host volumes, current-user WSL2 registrations and known Docker Desktop paths |
| Budgets | Per-resource advisory byte limits, warning/critical percentages and host free-space reserve |
| Monitoring | Background metadata snapshots, tray alerts, cooldown and recovery hysteresis |
| Settings | Validated persistent configuration; corrupt files are not silently overwritten |
| Docker cleanup | Opt-in old dangling images, build cache and explicitly disposable-labeled stopped containers |
| WSL cleanup | Manually confirmed `apt-get clean` for a named registered non-Docker distro |
| Compaction | Manually confirmed shutdown, verified offline backup, then native `CompactVirtualDisk` |
| History | Durable maintenance journals and backup manifests in application data / backup destination |

The information panel labels logical VHDX length separately from physical allocation and guest
usage, which are not inferred. Budgets are alerts, **not hard filesystem quotas**. Unknown
measurements are not treated as zero. Monitoring does not start WSL or Docker.

## Safety and supported configurations

- No volume deletion, ordinary stopped-container deletion, broad system prune or personal-file cleanup.
- Automatic cleanup is off by default; category opt-ins and minimum ages are required.
- Docker must be a verified reachable local Desktop engine. Remote/unverifiable engines are refused.
- Manual WSL cache cleanup can start its named distro, as disclosed in the approved plan.
- Compaction requires administrator privileges, a recognized dynamic VHDX and sufficient space
  for a full verified backup on a **different fixed volume**. All WSL/Docker interruption is disclosed.
- Compaction is synchronous/non-preemptible. Do not restart WSL/Docker, suspend or interrupt the
  machine until completion. Exit waits for maintenance; there is no forced cancellation or retry.
- Windows/daemon checks are the final guards during the documented metadata-handle handoff;
  other clients are not transactionally excluded. Unsupported or changed targets are refused.
- Backups are never silently overwritten, deleted or restored. No minimum final size or amount
  of recovered space is guaranteed. Narrow cleanup is irreversible and can finish partially.

Windows 10/11 x64 is the target. Current-user default/registered WSL2 and supported local Docker
Desktop configurations are covered; custom Docker layouts, other users, remote engines and
unsupported storage layouts are not claimed as discovered. See [Windows usage](native/docs/windows.md),
[monitoring details](native/docs/core.md) and [maintenance details](native/docs/maintenance.md).

## Build from source

Development only: install a C++17 compiler, CMake and the Windows SDK (Visual Studio 2022 C++
Desktop tools work). The installed application does not need those development tools.
From a local Windows checkout:

```powershell
cmake -S native -B build/native
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File native/tests/shell_checks.ps1 -Executable build/native/Release/WslCleaner.exe
```

MSVC release builds use the static C++ runtime. CMake also supports portable policy checks on
Linux. Packaging: `native/packaging/Package.ps1 -Executable <exe> -OutputDirectory <new-directory>`.

## Verification boundary

Observed: 18/18 portable core groups, 19/19 portable maintenance groups (20 groups on Windows,
including redirected native UTF-16 output), Windows CTest 3/3, shell/API checks 16/16 and
isolated install/uninstall preservation/refusal checks 4/4. The Chinese UTF-16 and Arabic
UTF-8 decoding regressions were reproduced before the fix and pass afterward. Independent
static verification found no demonstrated severe candidate-caused blocker; it is not an
end-to-end production safety guarantee.
The native API check creates, compacts and removes its own empty Unicode-named VHDX in the
OS temporary directory; it never attaches a disk or accesses real WSL/Docker data.
No production Docker pruning, distro cleaning or real WSL/Docker compaction was executed on
the developer's working environment. These workflows require user consent and their runtime
prerequisites. Interactive visual quality, all Windows/storage layouts, attached/in-use refusal
and crash recovery are not exhaustively verified. Build warnings from temporary/UNC development
paths were observed; native builds/tests completed successfully after integration fixes.

Feature tracking: [`odd/tasks/native-windows-app.md`](odd/tasks/native-windows-app.md).
