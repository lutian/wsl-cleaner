# Native maintenance

Implementation for T3 / S2, S7 and S9. Verified with fake-driven policies and
isolated native Windows API tests, not by operating on working WSL/Docker data.

## Native API verification

Scoped correction: parent observed job23 RED exit 1, 15/17 passing, with only
optional-platform and WSL-byte-decoding groups failing. Helpers are now wired;
post-change GREEN is pending. Subtype 3 remains dynamic (2 fixed, 4 differencing),
pinned by the production helper and disposable fixture per Microsoft:
https://learn.microsoft.com/en-us/windows/win32/api/virtdisk/ns-virtdisk-get_virtual_disk_info

Parent observed the status-contract RED at **12/14 PASS, exit 1** (job19), with
only `native-status-success` and `native-status-failures` failing as intended.
The native API switch is now implemented: synchronous `CompactVirtualDisk`,
locale-independent DWORD results, UTF-16 source paths, and no localized output
parser or English/ASCII precondition. Portuguese Windows is not excluded.
The parent's `initguid.h` immediately before `virtdisk.h` is preserved.

Post-change portable GREEN passed 14/14. MSVC Release build and Windows CTest
passed all three executables, including settings/policy checks, maintenance fakes,
and an isolated native Unicode create/open/detached/compact/dispose check against
a newly created empty dynamic VHDX. It never attaches a disk or accesses real
WSL/Docker data. This is not full end-to-end proof of the production backup and
compaction workflow, attached/in-use refusal or crash durability.

## Supported production paths

### Explicitly enabled Docker categories

The native adapter validates the user's selected context and official local Linux
Docker Desktop backend, then delegates eligibility to these **narrow daemon-side
collection APIs**, never client-selected object deletion:

```
image prune --force --filter until=<minimum_age_days * 24>h
builder prune --force --filter until=<minimum_age_days * 24>h
container prune --force --filter label=wsl-cleaner.disposable=true --filter until=<minimum_age_days * 24>h
```

Each category requires its own configuration opt-in. No `system prune`, volume
operation, `--all`, `-a`, force-rm, image-rm, ancestor pruning or ordinary stopped
container deletion exists in this implementation. `--force` suppresses the CLI
prompt; it does not force removal of an in-use object. Images are dangling-only
by the daemon's default; build cache is unused/recreatable by the builder's
contract. Container pruning requires BOTH the exact label value `true` and the
age predicate; the daemon selects stopped containers at operation time. Different
Docker categories apply `until` to their documented creation/unused-age semantics.
Zero configured days explicitly permits zero age; the default is thirty days.

The preview identifies enabled categories, threshold and **irreversibility**.
Typed `CLEAN` plus a matching issuer token authorizes execution. Before each
category the adapter refreshes local context/server/running-state validation;
the daemon evaluates its own reference/tag/label/running/age predicates during
prune. Unsupported command/filter versions, daemon refusals, timeout or malformed
output cause an honest incomplete result; there is no broader fallback or retry.
Counts in Result are completed operations/categories, NOT inferred deleted-object
counts or promises about bytes recovered.

### Manual WSL apt cache

Only a registered, non-Docker distribution can receive this fixed argv through
the trusted Windows `wsl.exe`:

```
--distribution <literal registered name> --user root --exec /usr/bin/apt-get clean
```

There is no shell, PATH-based guest executable lookup, package installation or
arbitrary guest command. Native `WslIsDistributionRegistered` is resolved from the
system WSL API DLL; registration/ownership are rechecked immediately before exec.
Known Docker Desktop distributions are rejected. Missing WSL APIs, unavailable
apt-get, unregistering a distro or tool errors fail closed.

The preview names the selected distro and states that **CLEAN may start it if it
is stopped after confirmation**. This is explicit named manual-start consent,
not an invisible start by monitoring. The API rejects apt plans when
`auto_cleanup=true`; disable automatic cleanup before requesting manual apt.
Monitoring/inventory and Docker cleanup do not launch a distro. There is no
production adapter selection switch in environment/configuration.

### Offline backup and compaction

The compactor is the Windows Virtual Disk API `CompactVirtualDisk`, called
synchronously with flags NONE and no OVERLAPPED operation. Structured DWORD
return values, not localized text or process exit codes, determine its result.
No command script, fallback tool, attachment or forced-detachment trick exists.
The native API must itself refuse read/write-attached/in-use disks. The sequence is:

1. Require administrator rights. Match a measured WSL/Docker VHDX path in fresh
   `Collect()` inventory, obtain volume/file identity and length, reject reparse
   traversal, unsupported paths and fixed/differencing/unknown VHD types.
2. Verify an existing backup directory on a **different local fixed volume**,
   unchanged directory identity, sufficient free space for the entire logical
   source plus configured `host_reserve` and 64 KiB manifest allowance. Backups
   are full streamed copies, not sparse-copy space estimates.
3. Preview exact target and backup directory, disclose shutdown of **ALL WSL
   distributions**, and warn the user not to restart Docker/WSL during maintenance.
   Require typed `COMPACT`, exact issuer token/plan/config and
   `Approval.allow_shutdown=true`.
4. Revalidate admin, known target identity/size, destination, local Docker,
   absence of running containers, and the trusted official Docker Desktop
   `desktop stop` plugin/help capability. Journal before clean Docker stop, then
   before official `wsl.exe --shutdown`. Never kill Docker/WSL host processes,
   start their daemons, or restart them automatically.
5. Confirm detached state via `GetVirtualDiskInformation(IS_LOADED)`. Hold a
   share-mode-zero source handle and pinned path components. Recheck file/volume
   identity and size while streaming the backup, hash original bytes with native
   BCrypt SHA256, flush the backup, independently read it back and compare SHA256
   and byte count. Recheck space reserve during copying and before manifest.
6. Create a non-overwritten UTF-8 manifest recording token, time, source and backup
   paths/identities/volumes, byte count, SHA256 and verified status. Flush it before
   permitting compact. Both files use CREATE_NEW. Names are
   `backup-<token>.vhdx` and `backup-<token>.manifest` in the approved directory.
   **Never delete, overwrite, retry or automatically restore a backup**, including
   a partial backup after failure.
7. Journal compact. Verify the held source guard; release its file handle, repeat
   detached verification, reacquire an exclusive file handle and compare the
   original identity/size a second time. Close only that final file handle for
   the read/write native metadata-handle handoff. Parent directories remain pinned.
8. Open the VHDX using `OpenVirtualDisk` version 2, GetInfoOnly FALSE and ReadOnly
   FALSE, without attaching it. Retain a separate FILE_READ_ATTRIBUTES identity
   handle that permits data reads/writes but not path deletion; this is explicitly
   **not** a continuing exclusive source-data handle. Recheck original file/volume
   identity and size, then check detached state on the actual native compaction
   handle immediately before `CompactVirtualDisk`.
9. Accept only returned DWORD `ERROR_SUCCESS` (zero). Every nonzero status is an
   exact unsigned numeric failure, without retry, cancellation, fallback or restore;
   source mutation may be partial. On zero, recheck detached state, close metadata
   and attribute handles, then recheck detached/exclusive source identity and volume
   before recording completion. Any post-check failure remains an incomplete result
   with the backup and interrupted journal preserved.

#### Explicit compaction prerequisites / unsupported configurations

Source and backup paths remain UTF-16 and manifests UTF-8; no UI-language or ASCII
path gate exists. Portuguese/other localized Windows installations use the same
native API and DWORD contract. Existing unambiguous local path checks and their
30000-character bound remain; unsupported UNC/device/reparse paths are not enabled.
Actual native API/Unicode behavior still needs parent-owned isolated Windows
verification; portable fake coverage is not proof of an executed host operation.

Require standalone dynamically expanding VHDX on accessible fixed volumes.
Unrecognized/custom locations absent from `Collect`, reparse/UNC/removable paths,
differencing/fixed images, unknown attachment state, no second suitable volume,
insufficient reserve, absent/old Docker Desktop clean-stop plugin, or an inactive/
unverifiable Docker backend are refused. The adapter will not start Docker just
to establish the no-running-containers check. It may also refuse an in-use VHDX
whose metadata cannot be safely queried before preparation.

## Concurrency and identity scope

The secured `Global\\WslCleaner.NativeMaintenance.v1` mutex serializes application
instances across processes/sessions/users. Its DACL is validated before acquisition;
unavailable/insecure/abandoned objects fail closed. It is held through the whole
operation. It is **not** an exclusive Docker-daemon transaction.

The accepted boundary assumes no adversarial external same-user actor. Ordinary
concurrent clients can still create new objects or restart workloads. Daemon-side
filtered pruning and OS/daemon reference checks avoid the prior client-ID label/
tag TOCTOU deletion design. Commands are independent, not an atomic transaction;
refusals/partial results remain possible.

The source file really is exclusive during backup. It is **not** claimed to stay
exclusive after release for the native metadata handle. There is a residual
exclusive-to-provider handoff gap, disclosed in the plan. Revalidation of source
identity/size, detached state on the actual provider handle, and the native API's
own in-use/read-write attachment refusal are the last guards. The retained
attribute handle prevents path replacement but does not prohibit data access.
An external Docker/WSL restart can make compaction fail; no forced detachment/retry
is attempted. Backups remain available. This implementation does not defend against
a malicious process under the same user identity replacing configuration/
registrations or falsifying tool state.

## Trusted process boundary

Native C++17; no managed application dependency, general shell, installer, downloaded
helper or PowerShell script. System tools come from `GetSystemDirectoryW`, not PATH.
Docker executables/plugins come from the OS Known Folder Program Files Docker
Desktop installation. Their file/path components are pinned without reparse
traversal; cached Authenticode trust must name publisher exactly `Docker Inc` or
`Docker Inc.`. Certificate revocation is not fetched online.

Only official local `docker_engine` / `dockerDesktopLinuxEngine` named pipes are
accepted. The server PID must belong to the pinned, trusted installed
`resources\\com.docker.backend.exe`. The selected context/endpoint, backend PID
and Linux server version are pinned to service lifetime; platform display name is optional. A change requires
a new service and new approval. All `DOCKER_*` and `BUILDX_*` environment overrides
are rejected, including TLS/config/context overrides. No daemon start is issued.

Context show/inspect use only fixed builtin CLI commands against the user's
configuration, rejecting remote endpoints. Operational commands use explicit
`--host` and a staged restricted `--config` under the verified application data
directory. Its exact immutable payload contains only the trusted official plugin
directory. The file is CREATE_NEW, flushed, then content-checked and pinned for
every command; modification/incomplete creation is a refusal, never a permissive
fallback. `docker version` is used instead of `docker info` to avoid executing
arbitrary discovered plugin metadata. Only the explicitly verified official
Desktop plugin can be dispatched for stop/help. No user-selected plugin is used.
Preparation may stage this non-destructive CLI metadata; it never cleans/stops/
shuts down/compacts anything.

`CreateProcessW` receives an explicit executable and correctly escaped, always-
quoted Windows argv. Untrusted data is never shell code. NUL/oversized arguments
are rejected. Restricted inherited handle lists and kill-on-close jobs constrain
children. Combined output is capped at 1 MiB. Docker remains strict UTF-8. Only
trusted system wsl.exe output recognizes UTF16LE BOM/leading LE units or valid
forwarded UTF-8; malformed sequences and decoded NULs are refused. Read-only
CLI commands get 15 seconds; prune/apt/clean-stop/WSL-shutdown get 120 seconds;
job cleanup waits up to five additional seconds for the direct child. Source
copy/readback checks a six-hour transaction deadline between chunks. Windows
metadata/trust calls, synchronous disk I/O and flushes are not preemptible timeouts.
A timed-out CLI may already have changed daemon state; results report unknown/
partial outcome, retain the journal, and never retry automatically.

`CompactVirtualDisk` is a synchronous **non-preemptible** native metadata operation,
not a child process. There is no hard timeout, cancellation, job kill or compactor
retry. Its preview discloses this before consent. It runs on the existing maintenance
worker while UI processing remains separate; the global maintenance guard and
metadata/identity guards stay held until the call returns. The backup chunk deadline
does not cancel or time-limit native compaction. A lengthy operation must not be
interpreted as safe permission to restart Docker/WSL or kill the application.

## Issuer and durable journal

`MaintenanceService` owns its adapter and Engine through the parent-owned pimpl.
Prepare validates shared `ValidateConfig`, issues an OS-random 256-bit token and
stores the immutable Plan and every Config field. Execute compares every field,
requires exact CLEAN/COMPACT approval, checks clock rollback and the exact
300-second expiry boundary, acquires the global guard, and checks freshness again.
The first mutation must begin within that window; a consistent long backup may
finish later without granting replay permission. Tokens are consumed before any
mutation attempt. There are at most 64 outstanding / 4096 lifetime-issued tokens
per service, with no token reuse; exhaustion is an explicit error.

Reuse `DataDirectory()` and verify its location against OS Known Folder
LocalAppData; reject environment redirection and reparse components. Before the
first destructive operation create a fresh `maintenance-<token>.journal`, record
plan action/target/backup/expiry/age, and flush. Flush pending markers before each
operation and completion markers afterward. A final `transaction-complete` marks
success; missing completion remains interrupted. No journal is truncated, deleted,
resumed or converted into success at restart. Historical records are preserved;
UI discovery/recovery of those records is not implemented in this worker surface.

Budgets/reserves are advisory and no minimum achievable VHDX size or bytes saved
are guaranteed. Failure reports completed operations and retains artifacts; a
failed result does not mean the host was rolled back.

## Parent-run validation only

The parent observed the original stub RED: **0/9, exit 1**, and subsequently
reported the preceding eleven-group suite at **11 PASS** (job13). The suite now
has fourteen groups: all eleven existing groups and their safety-ordering checks
remain, plus `native-status-success`, `native-status-failures` and
`unicode-compact-policy`. Native status checks require zero to succeed with an
empty diagnostic and nonzero DWORDs 5, 32, 5023, 997 and 4294967295 to fail with
exact unsigned numeric diagnostics, preserved backup/interrupted status and no
retry/replay. Unicode source/backup paths are checked through plan, preview,
journal and fake adapter boundaries; that alone does not establish native API
or localized Windows support.

Parent observed **12/14 PASS, exit 1** in job19 against the deliberate status stub;
only the two native-status groups failed as intended. That stub is now implemented,
the obsolete script builder/assertions removed, and Unicode policy flow uses the
native zero-status fake path. Existing eleven-group ordering/refusal assertions
remain, including backup before compact and no retry/replay. The preview additionally
pins disclosure that native compaction has no hard timeout or cancellation.

No post-change build or GREEN run was performed by the writer. Fakes and pure
helpers never invoke a host adapter, user Docker/WSL, elevation, backup or compaction.

From repository root:

```sh
scratch=$(mktemp -d) && g++ -std=c++17 -Wall -Wextra -Werror -pedantic -I native/include native/src/policy.cpp native/src/maintenance.cpp native/src/maintenance_policy.cpp native/tests/maintenance_checks.cpp -o "$scratch/maintenance_checks" && "$scratch/maintenance_checks"
```

Observed post-change GREEN: **14/14, exit 0**. Requires core's
shared functional ValidateConfig; no duplicate settings validator exists here.
Windows rebuild and post-change native checks are parent-owned using the installed
Visual Studio 17 MSVC compiler selected by CMake, even when it is absent from PATH.
The parent diagnosed Zig crashes; do not substitute that failed toolchain for the
available native compiler. No Windows build or host maintenance command was run
by this writer.

Windows maintenance link additions: `bcrypt`, `wintrust`, `crypt32`, `advapi32`,
`shell32`, `ole32`, `uuid`, **`virtdisk`**, in addition to normal kernel32/C++ and
other modules' UI libraries. WSL registration is dynamically resolved, no wslapi
import library needed. No shared-header edits are required. Parent owns compiler
scratch cleanup. Portable fakes cannot prove Windows ACLs, publisher/pipe/plugin
identity, Virtual Disk native API behavior, job cleanup, or crash durability; those
remain isolated Windows verification work, never tests on the working user host.
