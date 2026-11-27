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

## Panel guide: what each control does

This guide describes the current native v1 panel. **The selected disk does not make all
settings disk-specific.** Only its budget is disk-specific; thresholds, timing and Docker
cleanup options are global. The information area is a technical report, not an event log.

### Quick setup: monitor without deleting anything

1. Leave all four Docker cleanup checkboxes unchecked. Leave `AllowShutdown` unchecked
   and the confirmation text empty. Monitoring does not need maintenance consent.
2. Select the WSL distribution or an actually measured Docker data disk. Check its name,
   `path` and `logical` value in the report. Do not configure a missing candidate as if it
   were a real measured disk; see [Why several Docker entries appear](#why-several-docker-entries-appear).
3. Enter that disk's alert budget in `Budget bytes (0 = unset)`. Use digits only, without
   `GB`, commas or decimal separators. This is a warning threshold, not reserved space or
   a limit that stops the disk growing. Choose a value appropriate to your available space;
   a budget below the current file length can immediately trigger an alert.
4. For an initial monitoring setup, keep `Warn %` at `80`, `Critical %` at `90`,
   `Poll sec` at `120` and `Cooldown sec` at `1800`. These settings apply to every disk
   with a saved budget. Read the separate host-reserve behavior below before setting it.
5. Click `Save settings` **before changing the selected disk**. Repeat steps 2–5 for each
   disk you want to monitor. Save before the next refresh: the current UI can reload an
   unsaved budget when the selection or inventory changes.
6. Close the window with **X** to leave monitoring running in the tray. Use
   `Exit (waits for maintenance)` to stop the app completely.

Byte examples (binary units; these are examples, not recommendations for your disks):

| Budget | Enter exactly |
|---|---|
| 20 GiB | `21474836480` |
| 50 GiB | `53687091200` |
| 100 GiB | `107374182400` |
| 150 GiB | `161061273600` |

For a 100 GiB budget with 80% warning and 90% critical thresholds, warnings begin at
80 GiB of **VHDX file length**, and critical alerts at 90 GiB. This does not measure the
percentage used inside Linux or the VHDX's virtual maximum capacity.

### Reading the information area

| Item in the report | Meaning |
|---|---|
| `Observed` | Time of the snapshot, currently printed as Unix seconds, not a friendly date. |
| `Host`, `capacity`, `free` | A Windows fixed volume, its total capacity and its free bytes. A `Volume{...}` path can be a small system volume, not another WSL/Docker disk. |
| `Disk`, `kind`, `path` | Resource name, WSL/Docker classification and VHDX file path. |
| `logical` | Current VHDX file length in bytes. **Not** physical allocation, Linux guest usage or recoverable space. |
| `budget` | Saved alert budget for that resource; `unset` means no per-disk budget alert. |
| `unknown` | Measurement unavailable: the file can be absent, inaccessible or rejected by safety checks. It does not mean zero bytes. |
| `Note` | Discovery limits, unavailable paths and measurement warnings. |
| Status / reviewed plan | Operation results or the proposed maintenance steps. These currently share the report area with inventory; it is not a chronological history. |

### Disk selection, budget and saving

| Control | Scope | What it does |
|---|---|---|
| Disk selector above the settings | Selected disk | Chooses which disk budget you edit and which target WSL cache cleanup or compaction uses. It does **not** scope Docker pruning to that VHDX. The current fixed-width selector can truncate a long path; the full `path` also appears in the report. |
| `Budget bytes (0 = unset)` | Selected disk only | Alert budget in bytes. `0` removes this disk's budget; it does not delete, resize or disable the disk. |
| `Save settings` | **All global fields + selected disk budget** | Validates and persists the current global fields/checkboxes and the budget of the selected disk. Previously saved budgets for other disks remain. It does not execute manual maintenance, but enabling automatic cleanup here can authorize later automatic runs. |

Changing selection reloads that disk's saved budget and invalidates a prepared plan.
Inventory refreshes can also reload an unsaved budget/invalidate a plan in the current UI.
Save each disk before switching; do not assume that selecting several disks saves them.
Settings are stored at `%LOCALAPPDATA%\WslCleaner\settings.conf`.

### Global monitoring settings

These fields are **not** separate configurations for each selection.

| Field | Meaning and valid values |
|---|---|
| `Reserve bytes` | Minimum desired free space on each monitored Windows host volume, in bytes. It does not reserve/allocate space. `0` disables host-reserve alerts only; disk-budget alerts still work. The default is 20 GiB. This applies to small fixed system volumes too, which can produce confusing warnings; it is not a reserve specifically for the selected VHDX. Warning occurs below the reserve; critical at half the reserve or less. |
| `Warn %` | Warning percentage of each disk's saved budget, 1–99 and strictly below `Critical %`. Default: `80`. |
| `Critical %` | Critical percentage of each disk's saved budget, above `Warn %` and at most 100. Default: `90`. |
| `Poll sec` | Seconds between inventory snapshots, 10–86400. Default: `120` (two minutes). |
| `Cooldown sec` | Seconds before repeating an alert, 0–604800. Default: `1800` (30 minutes). A severity escalation can alert earlier; recovery hysteresis avoids repeated warning/recovery oscillation. This also limits automatic Docker cleanup attempts, with a minimum effective 60-second attempt interval. |

### Docker cleanup checkboxes and age

These options are global to the validated local Docker engine, **not the selected disk**.
Leave them off while configuring monitoring. Category choices apply to manual Docker plans
too; checking a category alone does not enable automatic cleanup.

| Control | What enabling it means |
|---|---|
| `Min age days` | Minimum age of eligible Docker objects, 0–36500 days; default `30`. It is not the age of the VHDX and does not govern `apt-get clean`. It is combined with the category's daemon-side filters, not a guarantee that every object of that age is deleted. |
| `Opt in: auto Docker cleanup on Docker alert` | Allows later automatic cleanup after a Docker budget alert, **only** with a category opted in and valid saved settings. Host free-space reserve alerts alone do not trigger it. No automatic WSL cleanup or compaction. |
| `Build cache` | Allows cleanup of eligible old, unused Docker build cache. A future image build may need to rebuild/download that cache. |
| `Dangling images` | Allows cleanup of eligible old dangling, unused images. This does not mean all old images or every image without a running container. |
| `Labeled disposable containers only` | Allows deletion only of eligible old, stopped containers with the exact label `wsl-cleaner.disposable=true`. Ordinary stopped containers and volumes are excluded. |

**Cleanup is irreversible and can complete partially.** Do not enable automatic cleanup
unless you understand the selected categories and accept their consequences.

### Manual maintenance buttons and confirmations

Preparing a plan is not permission to execute it. Review the displayed target, steps,
shutdown impact, backup location and expiration before approving it.

| Control | What it does / what it affects |
|---|---|
| `Plan Docker cleanup` | Prepares a plan for the validated local Docker engine using saved categories/age. It is independent of the disk selector and does not compact a VHDX. |
| `Plan WSL apt cache` | Prepares `apt-get clean` for the selected registered non-Docker WSL distribution, by **distribution name**. Removes downloaded APT package archives, not personal files or installed applications. Execution can start this named distro as root; this is disclosed in the plan. Selecting a Docker disk is not valid for this action. |
| `Plan offline compact` | Prepares compaction of the selected VHDX path. Requires running the app as administrator, choosing an existing backup directory on a different fixed volume, and satisfying identity/offline/free-space/backup checks. Does not promise a minimum final size. |
| `Exact CLEAN / COMPACT:` | Type exact uppercase `CLEAN` for a cleanup plan or `COMPACT` for a compaction plan. This is consent for the reviewed plan, not a general setting. |
| `AllowShutdown: ALL WSL + Docker workloads stop` | Additional compaction consent: **all** WSL distributions and Docker workloads may be stopped, not just the selected disk. It is not needed to monitor or a general cleanup checkbox. |
| `Execute reviewed plan` | Executes a previously prepared plan with its required confirmation and checks. Plans are single-use, expire after five minutes, and can be invalidated by settings/selection changes or errors. Never type approval without first reviewing the plan. |
| `Exit (waits for maintenance)` | Stops the application. If work is active, waits for it safely instead of terminating it mid-backup/cleanup/compaction. |

Do not restart WSL/Docker or interrupt the computer during compaction. The app does not
silently elevate itself; explicitly exit the ordinary instance before starting an
administrator instance for this operation. Backups are not automatically deleted/restored.

### Window close and tray controls

- **X / close window:** hides to the tray when the tray icon is available; monitoring stays active.
- **Click tray icon / notification:** opens the panel. Alerts also request that the panel open.
- **Tray right-click → Show:** opens the panel.
- **Tray right-click → Exit:** exits with the same wait-for-maintenance behavior.
- **Startup:** not controlled by these checkboxes. It is a separate installer opt-in,
  `Install.cmd -EnableStartup`, during the first installation.

### Why several Docker entries appear

Four Docker entries do **not** prove that four data disks exist. The current inventory
includes registered Docker distributions and three known candidate locations:

| Display name | Candidate path / role |
|---|---|
| `docker-desktop` | Docker's registered WSL distribution; its path comes from registration. It is not automatically the main user-data disk. |
| `Docker data (modern)` | `%LOCALAPPDATA%\Docker\wsl\disk\docker_data.vhdx` |
| `Docker data (legacy)` | `%LOCALAPPDATA%\Docker\wsl\data\ext4.vhdx` |
| `Docker data (alternate)` | `%LOCALAPPDATA%\Docker\wsl\data\docker_data.vhdx` |

A registration called `docker-desktop-data` can also appear. Equal normalized paths are
merged, but candidates with different paths remain, even when unmeasured. Check the actual
`path` and whether `logical` has a measured value. An `unknown` legacy/alternate entry is
not proof of a duplicate backup, garbage or a safe deletion target. **Do not delete any
Docker directory or WSL registration just to remove an entry from this list.** Custom
Docker locations may not be found. The current layout/discovery presentation needs clearer
separation of detected disks and unavailable candidates; this guide does not change it.

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
