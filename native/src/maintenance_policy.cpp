#include "maintenance_internal.hpp"
#include <limits>
#include <stdexcept>

namespace wc::maintenance {
namespace {
bool Same(const Config& a, const Config& b) {
    return a.poll_seconds == b.poll_seconds && a.warning_percent == b.warning_percent &&
        a.critical_percent == b.critical_percent && a.cooldown_seconds == b.cooldown_seconds &&
        a.minimum_age_days == b.minimum_age_days && a.host_reserve == b.host_reserve &&
        a.auto_cleanup == b.auto_cleanup && a.clean_build_cache == b.clean_build_cache &&
        a.clean_dangling_images == b.clean_dangling_images &&
        a.clean_disposable_containers == b.clean_disposable_containers && a.budgets == b.budgets;
}
bool Same(const Plan& a, const Plan& b) {
    return a.token == b.token && a.target == b.target && a.backup_directory == b.backup_directory &&
        a.action == b.action && a.steps == b.steps && a.expires == b.expires &&
        a.needs_shutdown == b.needs_shutdown && a.needs_admin == b.needs_admin;
}
bool Text(const std::wstring& s, bool empty = false) {
    return (empty || !s.empty()) && s.size() <= 32768 &&
        s.find_first_of(std::wstring(L"\0\r\n", 3)) == std::wstring::npos;
}
bool DockerSafe(const DockerState& state, std::wstring& error) {
    if (state.local_engine && state.local_context && !state.remote_environment) return true;
    error = L"Docker requires a verified local engine/context without environment overrides.";
    return false;
}
bool DistroSafe(Adapter& adapter, const std::wstring& target, std::wstring& error) {
    WslState state;
    if (!adapter.Wsl(target, state, error)) return false;
    if (state.registered && !state.docker_owned) return true;
    error = L"Manual apt clean requires a registered non-Docker distribution.";
    return false;
}
bool TargetSafe(const OfflineTarget& target) {
    return !target.identity.empty() && !target.volume.empty() && target.fixed_volume && !target.reparse && target.bytes;
}
} // namespace
std::vector<std::wstring> CleanupArguments(ObjectKind kind, unsigned days) {
    if (days > 36500) throw std::invalid_argument("Invalid cleanup age");
    const auto until = L"until=" + std::to_wstring(static_cast<std::uint64_t>(days) * 24) + L"h";
    switch (kind) {
    case ObjectKind::Image: return {L"image", L"prune", L"--force", L"--filter", until};
    case ObjectKind::BuildCache: return {L"builder", L"prune", L"--force", L"--filter", until};
    case ObjectKind::Container: return {L"container", L"prune", L"--force", L"--filter", L"label=wsl-cleaner.disposable=true", L"--filter", until};
    }
    throw std::invalid_argument("Invalid cleanup category");
}
std::vector<std::wstring> AptArguments(const std::wstring& name) {
    if (!Text(name) || name.size() > 256 || name.front() == L'-') throw std::invalid_argument("Invalid distro name");
    return {L"--distribution", name, L"--user", L"root", L"--exec", L"/usr/bin/apt-get", L"clean"};
}
Result CompactStatus(std::uint32_t status) {
    if (status == 0) return {true, L""};
    return {false, L"CompactVirtualDisk failed (DWORD " + std::to_wstring(status) +
        L"); backup retained, no retry. Operation may be partial."};
}
Engine::Engine(Adapter& adapter) : adapter_(adapter) {}
bool Engine::Prepare(Action action, const std::wstring& target, const Config& config,
    const std::wstring& directory, Plan& output, std::wstring& error) {
    std::lock_guard<std::mutex> hold(mutex_);
    output = {}; error.clear();
    if (!ValidateConfig(config, error)) return false;
    if (!Text(target, action == Action::DockerCleanup) || !Text(directory, action != Action::Compact)) {
        error = L"Invalid maintenance target or backup directory."; return false;
    }
    const auto now = adapter_.Clock();
    if (!now || now > std::numeric_limits<std::uint64_t>::max() - PlanLifetimeSeconds) {
        error = L"Maintenance clock is unavailable."; return false;
    }
    OfflineTarget identity;
    Plan plan;
    plan.action = action; plan.target = target; plan.backup_directory = directory;
    plan.expires = now + PlanLifetimeSeconds;
    switch (action) {
    case Action::DockerCleanup: {
        DockerState state;
        if (!adapter_.Docker(state, error) || !DockerSafe(state, error)) return false;
        plan.steps = {L"IRREVERSIBLE daemon-filtered cleanup on the validated local Docker engine; no volumes or ordinary stopped containers. --force only suppresses the tool prompt."};
        if (config.clean_dangling_images) plan.steps.push_back(L"Delete dangling unused images older than " + std::to_wstring(config.minimum_age_days) + L" day(s), daemon predicates evaluated at prune time.");
        if (config.clean_build_cache) plan.steps.push_back(L"Delete unused recreatable build cache older than " + std::to_wstring(config.minimum_age_days) + L" day(s).");
        if (config.clean_disposable_containers) plan.steps.push_back(L"Delete stopped containers older than " + std::to_wstring(config.minimum_age_days) + L" day(s) ONLY with exact label wsl-cleaner.disposable=true.");
        break;
    }
    case Action::WslAptCache:
        if (config.auto_cleanup) { error = L"Manual apt clean requires automatic cleanup to be disabled."; return false; }
        if (!DistroSafe(adapter_, target, error)) return false;
        plan.steps = {L"MANUAL CLEAN: will run /usr/bin/apt-get clean in " + target + L" as root; may start this named distro if stopped after confirmation. Monitoring/automation never starts WSL."};
        break;
    case Action::Compact: {
        if (!adapter_.Administrator()) { error = L"Compaction requires administrator rights."; return false; }
        if (!adapter_.Target(target, identity, error) || !TargetSafe(identity)) {
            if (error.empty()) error = L"Known VHDX identity or fixed-volume safety could not be established.";
            return false;
        }
        if (!adapter_.BackupDestination(directory, identity, config.host_reserve, error)) return false;
        DockerState state;
        if (!adapter_.Docker(state, error) || !DockerSafe(state, error) || !adapter_.CanStopDocker(error)) return false;
        if (state.any_running_containers) { error = L"Stop all running containers before requesting compaction."; return false; }
        plan.needs_admin = plan.needs_shutdown = true;
        plan.steps = {L"COMPACT consent authorizes clean Docker stop and GLOBAL shutdown of ALL WSL distributions.",
            L"Target: " + target, L"Backup directory: " + directory,
            L"Do NOT restart Docker or WSL during maintenance. Verify detached identity and hold exclusive source access during SHA256-verified backup and manifest on a different fixed volume.",
            L"After a second offline/exclusive/identity check, release the file handle only for the official compactor, which must refuse attached/in-use disks. This handoff has a residual concurrency gap.",
            L"Native CompactVirtualDisk is synchronous and non-preemptible: no hard timeout or cancellation. Keep Docker/WSL stopped until completion.",
            L"Preserve backups and interrupted journal on failure; never retry or restore automatically."};
        break;
    }
    default: error = L"Unsupported maintenance action."; return false;
    }
    for (auto it = issued_.begin(); it != issued_.end();) {
        if (now >= it->second.plan.expires) it = issued_.erase(it); else ++it;
    }
    if (issued_.size() >= 64 || tokens_.size() >= 4096) { error = L"Maintenance issuer capacity reached; create a new service after completing outstanding plans."; return false; }
    plan.token = adapter_.FreshToken();
    if (!Text(plan.token) || tokens_.count(plan.token)) { error = L"Could not issue a fresh secure maintenance token."; return false; }
    const auto minted = adapter_.Clock();
    if (minted < now || minted > std::numeric_limits<std::uint64_t>::max() - PlanLifetimeSeconds) {
        error = L"Maintenance clock changed during preparation."; return false;
    }
    plan.expires = minted + PlanLifetimeSeconds;
    tokens_.insert(plan.token);
    issued_.emplace(plan.token, Issued{plan, config, minted, identity});
    output = plan; error.clear(); return true;
}
Result Engine::Execute(const Plan& supplied, const Approval& approval, const Config& config) {
    std::lock_guard<std::mutex> hold(mutex_);
    std::wstring error;
    if (!ValidateConfig(config, error)) return {false, error};
    const auto found = issued_.find(supplied.token);
    if (found == issued_.end() || !Same(found->second.plan, supplied) ||
        approval.token != found->second.plan.token || !Same(config, found->second.config))
        return {false, L"Unknown, modified, replayed plan, approval or changed settings; prepare again."};
    const Issued issued = found->second;
    const Plan& plan = issued.plan;
    const auto fresh = [&]() { const auto now = adapter_.Clock(); return now >= issued.created && now < plan.expires; };
    if (!fresh()) return {false, L"Maintenance plan expired or clock moved backwards."};
    if (approval.text != (plan.action == Action::Compact ? L"COMPACT" : L"CLEAN") ||
        (plan.needs_shutdown && !approval.allow_shutdown))
        return {false, L"Explicit CLEAN or COMPACT approval and required global-shutdown consent are missing."};
    auto serialized = adapter_.LockMaintenance(error);
    if (!serialized || !serialized->Verify(error)) return {false, error.empty() ? L"Global maintenance guard unavailable." : error};
    if (!fresh()) return {false, L"Maintenance approval expired while acquiring the guard."};
    issued_.erase(found); // One-shot before mutation, including partial failure.
    unsigned completed = 0;
    const auto fail = [&]() -> Result {
        return {false, L"Maintenance incomplete; completed " + std::to_wstring(completed) + L" step(s). " +
            (error.empty() ? L"Safety verification failed." : error) + L" Backups and interrupted journal are preserved."};
    };
    // Consent must START within five minutes. A consistent multi-GB backup can
    // outlast that window; it does not grant a new token or authorize a replay.
    bool journal_started = false;
    const auto journal = [&](const std::wstring& step) {
        if (adapter_.Clock() < issued.created) { error = L"Maintenance clock moved backwards."; return false; }
        if (!completed && !fresh()) { error = L"Consent expired before the first mutation."; return false; }
        if (!serialized->Verify(error)) return false;
        if (!journal_started) {
            if (!adapter_.Journal(plan.token, L"plan-action=" + std::to_wstring(static_cast<unsigned>(plan.action)), error) ||
                !adapter_.Journal(plan.token, L"plan-target=" + plan.target, error) ||
                !adapter_.Journal(plan.token, L"plan-backup=" + plan.backup_directory, error) ||
                !adapter_.Journal(plan.token, L"plan-expires=" + std::to_wstring(plan.expires), error) ||
                !adapter_.Journal(plan.token, L"plan-age-days=" + std::to_wstring(config.minimum_age_days), error)) return false;
            journal_started = true;
        }
        return adapter_.Journal(plan.token, step, error);
    };
    if (plan.action == Action::DockerCleanup) {
        DockerState state;
        if (!adapter_.Docker(state, error) || !DockerSafe(state, error)) return fail();
        for (const auto kind : {ObjectKind::Container, ObjectKind::Image, ObjectKind::BuildCache}) {
            const bool enabled = kind == ObjectKind::Container ? config.clean_disposable_containers :
                kind == ObjectKind::Image ? config.clean_dangling_images : config.clean_build_cache;
            if (!enabled) continue;
            DockerState current;
            if (!adapter_.Docker(current, error) || !DockerSafe(current, error)) return fail();
            const auto category = kind == ObjectKind::Container ? L"containers" : kind == ObjectKind::Image ? L"images" : L"build-cache";
            if (!journal(L"pending-prune-" + std::wstring(category)) || !adapter_.ScopedCleanup(kind, config.minimum_age_days, error)) return fail();
            ++completed;
            if (!adapter_.Journal(plan.token, L"done-prune-" + std::wstring(category), error)) return fail();
        }
    } else if (plan.action == Action::WslAptCache) {
        if (config.auto_cleanup || !DistroSafe(adapter_, plan.target, error) || !journal(L"pending-apt-clean") ||
            !adapter_.AptClean(plan.target, error)) return fail();
        ++completed;
        if (!adapter_.Journal(plan.token, L"done-apt-clean", error)) return fail();
    } else {
        OfflineTarget target; DockerState state;
        if (!adapter_.Administrator() || !adapter_.Target(plan.target, target, error) || !TargetSafe(target) ||
            target.identity != issued.target.identity || target.volume != issued.target.volume || target.bytes != issued.target.bytes ||
            !adapter_.BackupDestination(plan.backup_directory, target, config.host_reserve, error) ||
            !adapter_.Docker(state, error) || !DockerSafe(state, error) || state.any_running_containers || !adapter_.CanStopDocker(error)) return fail();
        if (!journal(L"pending-stop-docker") || !adapter_.StopDocker(error)) return fail();
        ++completed;
        if (!journal(L"pending-shutdown-wsl") || !adapter_.ShutdownWsl(error)) return fail();
        ++completed;
        auto offline = adapter_.LockOffline(plan.target, issued.target, error);
        if (!offline || !offline->Verify(error)) return fail();
        if (!adapter_.BackupDestination(plan.backup_directory, target, config.host_reserve, error) ||
            !journal(L"pending-backup") || !adapter_.Backup(plan.target, plan.backup_directory, *offline, error) ||
            !offline->Verify(error)) return fail();
        ++completed;
        if (!journal(L"pending-compact") || !offline->Verify(error) || !adapter_.Compact(plan.target, *offline, error)) return fail();
        ++completed;
        if (!adapter_.Journal(plan.token, L"done-compact", error)) return fail();
    }
    if (completed && !adapter_.Journal(plan.token, L"transaction-complete", error)) return fail();
    return {true, L"Maintenance completed " + std::to_wstring(completed) + L" operation(s), not a count of deleted objects. No volumes were deleted."};
}
std::wstring CompactApproval(const Plan&) { return L"COMPACT"; }
} // namespace wc::maintenance
