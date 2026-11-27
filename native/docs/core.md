# Native core

Scope: T2, S2/S4–S6 and the advisory/no-development-maintenance boundary of S9.
C++17, Win32 only in `src/core.cpp`; dependency-free policy in `src/policy.cpp`.
The shared header is parent-owned.

## Verification handoff

The parent reported compiled RED job11: exit 1, 2 passed / 16 failed against the
intentional policy placeholders. After that authorization, `policy.cpp` now
implements validation, deterministic snapshot formatting and stateful advisory
alerts. Observed GREEN: 18 portable policy groups passed. Native MSVC Release
build and Windows CTest passed, including isolated settings persistence/corruption
checks. Inventory/alternate-layout coverage is not exhaustive. No user WSL/Docker
maintenance was executed during development.

Parent's portable GREEN command (from repository root, output outside the tree):

```sh
scratch=$(mktemp -d /tmp/wsl-cleaner-core-checks.XXXXXX) && g++ -std=c++17 -Wall -Wextra -Wpedantic -I native/include native/src/policy.cpp native/tests/core_checks.cpp -o "$scratch/core_checks" && "$scratch/core_checks"
```

The executable returns 1 when any named check fails, 0 otherwise; stdout contains
PASS lines and the final `N passed; N failed` count, stderr contains individual
FAIL diagnostics. It has 22 portable check groups and two additional Windows-only
config groups. The parent owns the command-created scratch directory and cleanup.
No build products are written in the repository.

Windows checks must link `core.cpp` and `policy.cpp` with the test and Win32
`advapi32` support. They never call `Collect` or `DataDirectory`. Each filesystem
check creates its own directory beneath `GetTempPathW` and removes only that test
directory in teardown. Actual registry discovery, mounted volumes, the installed
app's settings and real WSL/Docker files are deliberately not tested or accessed.
A parent-provided isolated Windows runner is still required.

## Public policy contract pinned by the checks

Defaults: polling 120 seconds, warning 80%, critical 90%, host reserve 20 GiB,
notification cooldown 1800 seconds. All cleanup toggles default off. Budgets are
advisory, never filesystem quotas or permission to execute maintenance. Evaluation
only returns notifications; it does not change settings or launch anything.
The header currently has no startup preference. Core never registers startup;
persisting a user-controlled startup setting needs a parent-owned header change.

`ValidateConfig` clears its diagnostic on success; on failure it reports the first
invalid field in the order below. Configuration is never modified.

| Rule | Exact diagnostic |
| --- | --- |
| poll outside 10–86400 seconds | `poll_seconds must be between 10 and 86400.` |
| not 1 <= warning < critical <= 100 | `Thresholds must satisfy 1 <= warning_percent < critical_percent <= 100.` |
| cooldown above 604800 seconds (zero disables suppression) | `cooldown_seconds must be between 0 and 604800.` |
| minimum age above 36500 days (zero allowed) | `minimum_age_days must be between 0 and 36500.` |
| budget ID empty, over 32768 characters, or containing NUL/CR/LF/equals | `Budget IDs must contain 1 to 32768 characters without NUL, CR, LF or '='.` |
| budget zero | `Budget values must be greater than zero.` |

Host reserve may be zero (disabled) or any uint64 value. Budget values may be any
nonzero uint64, including UINT64_MAX. Stable inventory IDs are normalized lowercase
absolute local VHDX paths; consumers should use the returned ID verbatim rather
than constructing display-name identities. No GiB multiplication is required by
policy; tests cover integer percentage arithmetic near UINT64_MAX. Percentage
thresholds use ceiling division, not overflowing `bytes * percent`.

### Alerts

Only snapshots with nonzero `observed <= now` and `now - observed <= 2 * poll`
are fresh. Invalid config, unknown lengths, zero host capacity, impossible host
free > capacity, absent entries and stale/future/undated snapshots do not emit an
alarm or clear remembered state. Unknown is not zero usage or recovery.

Budget warning/critical trigger inclusively at their configured percentages.
A warning clears silently only below the warning threshold minus five percentage
points (floor the resulting percentage at 1). A critical alert retains critical
severity until below critical minus five points (floor at warning); it can then
become warning. Rearming after full recovery can notify immediately. Escalation
from warning to critical notifies immediately even during cooldown. Same-level
notifications are suppressed until `now - last >= cooldown`; clock rollback must
not underflow that subtraction. States are separate per stable identity.

Host reserve triggers warning at `free < reserve`, critical at
`free <= floor(reserve / 2)`. Active host severity does not downgrade until full
recovery: `free > reserve + ceil(reserve / 10)`, using saturated uint64 addition.
Recovery silently rearms. A critical alert can escalate during warning cooldown.
Zero reserve disables host alerts. Host IDs are `host:` plus the supplied volume
root; budget IDs are `budget:` plus the stable VHDX ID.

Exact alert messages, with decimal byte counts:

- `Budget warning: Ubuntu (80 / 100 B).`
- `Budget critical: Ubuntu (90 / 100 B).`
- `Host reserve warning: C:\ (19 B free; reserve 20 B).`
- `Host reserve critical: C:\ (10 B free; reserve 20 B).`

`Alert::critical` must reflect severity, independently of message text. Format
output is deterministic LF-terminated plain text; check fixtures pin every byte,
including explicit `logical=unknown`, `budget=unset`, advisory labels, notes and
unknown host capacity/free values. VHDX lengths are logical file lengths, not
allocated blocks, guest filesystem utilization or recoverable bytes.

## Windows metadata inventory

`Collect` enumerates mounted fixed volumes, deduplicated by volume identity (not
drive letters), including both drive-letter and folder mounts. It queries mount
paths once with a bounded 32768-wchar buffer and uses the first path as the label.
A successful empty mount list silently omits the volume without querying space.
A failed lookup also omits it, adding `Host mount paths unavailable: ` followed by
the volume identity; neither case can create a reserve alert. Small or system
volumes are still included if mounted. Capacity and actual total free bytes come
from `GetDiskFreeSpaceExW` queried by volume GUID, using total free bytes rather
than caller-available bytes. Deterministic callback checks exercise this same
collection helper and `AlertEngine`, without enumerating real volumes.
Unknown capacity remains zero in the API and must display as unknown, not as a
measured empty disk. It enumerates the current user's HKCU Lxss distribution keys,
requires Version=2, reads DistributionName/BasePath/VhdFileName and defaults only
an absent VhdFileName to ext4.vhdx. Empty, malformed or unreadable filename values
remain unknown rather than guessing a different target. Registered docker-desktop distributions
are labeled Docker rather than ordinary WSL.

Known Docker candidates beneath LOCALAPPDATA:

- `Docker\wsl\disk\docker_data.vhdx` (modern)
- `Docker\wsl\data\ext4.vhdx` (legacy)
- `Docker\wsl\data\docker_data.vhdx` (alternate)

Paths use absolute lexical normalization and invariant lowercase; registered and
known-path candidates share one deduplication function. Missing candidates remain
visible with unknown length. Unsupported/relative/UNC paths produce notes, not
invented measurements. Inventory does not inspect guest filesystems, spawn WSL,
Docker or PowerShell, or determine that an installation is absent solely from a
missing registry key/path. Custom Docker locations outside known/registered paths
may not be found; this limitation is disclosed in the snapshot.

Before accessing a local file, existing path components are checked for reparse
points and the drive must be fixed. The leaf is opened for attributes with
OPEN_REPARSE_POINT and inspected again, and logical length comes from
`GetFileSizeEx`. These are cautious metadata guards, not an adversarial-race-proof
filesystem sandbox: ancestor replacement between checks remains a Windows
integration review item. There is no write access to inventory targets.

## Settings persistence

`DataDirectory` returns `%LOCALAPPDATA%\WslCleaner` without creating it (empty if
LOCALAPPDATA cannot be resolved). A caller supplies the settings filename, e.g.
`settings.ini`. Neither loading, inventory nor reading DataDirectory writes
settings. Missing/unreadable/corrupt settings return false, a nonempty diagnostic,
and leave both caller Config and stored bytes untouched; the UI must present the
error rather than silently saving defaults. Only explicit SaveConfig persists.

Version 1 is strict UTF-8 without BOM, NUL or CR, with LF-terminated key=value
lines. No comments, blank lines, whitespace trimming, signed numbers, trailing
junk, unknown keys, duplicate keys or omitted required fields. Unsigned decimal
values have overflow checks. Boolean values are 0 or 1. Required keys are:

```text
version=1
poll_seconds=120
warning_percent=80
critical_percent=90
cooldown_seconds=1800
minimum_age_days=30
host_reserve=21474836480
auto_cleanup=0
clean_build_cache=0
clean_dangling_images=0
clean_disposable_containers=0
```

Additional `budget.<stable-id>=<bytes>` lines are allowed, sorted by ID on save.
The file is capped at 4 MiB. Load parses and validates a local candidate before
assigning it to the caller; invalid saves return before filesystem modification.
Save validates, creates the parent directory only on explicit save, writes a
uniquely reserved CREATE_NEW sibling temp file, flushes it and renames with
MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH). Failed writes/renames remove only
the owned temporary file; existing settings are not intentionally overwritten on
failure. There is no delete-then-create fallback. UNC/mapped-network and reparse
paths are refused. Cross-process saves are last-successful-rename-wins, never a
shared temporary-file overwrite. Power-loss/filesystem-specific rename guarantees
and malicious ancestor races require separate Windows integration review.

## Remaining proof / next phase

Parent must run portable GREEN and the isolated Windows config groups and native
build. The config tests do not cover registry discovery, real volume enumeration,
reparse races or custom Docker locations. The Windows code was written without
host interaction; it is not evidence of a passing Windows integration. Settings
load rechecks the opened handle for directory/reparse attributes, and registry
reads reject type/size changes between queries. No startup API, scanner injection
seam, UI behavior or maintenance behavior was added by this core-only phase.
