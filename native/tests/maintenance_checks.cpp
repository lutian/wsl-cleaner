#include "maintenance_internal.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace wc;
using namespace wc::maintenance;
namespace {
void Require(bool value, const char* error) { if (!value) throw std::runtime_error(error); }
struct Guard final : OfflineGuard {
    bool& valid;
    explicit Guard(bool& value) : valid(value) {}
    bool Verify(std::wstring& error) override { if (!valid) error = L"Lost guard"; return valid; }
};
struct Fake final : Adapter {
    std::uint64_t now = 2000000000;
    unsigned sequence = 0;
    bool lock = true, admin = true, destination = true, stop_supported = true;
    bool stop_ok = true, shutdown_ok = true, offline = true, backup_ok = true;
    bool lose_guard_after_backup = false, journal_ok = true, compact_ok = true;
    bool prune_ok = true, apt_ok = true;
    bool native_status_contract = false;
    std::uint32_t compact_status = 0;
    std::wstring target_seen, backup_source_seen, backup_directory_seen, compact_seen;
    DockerState docker{true, true, false, false};
    WslState wsl{true, true, false};
    OfflineTarget target{L"volume-1:file-42", L"volume-1", 1024, true, false, true};
    std::vector<std::wstring> effects;
    std::vector<std::vector<std::wstring>> commands;
    std::uint64_t Clock() override { return now; }
    std::wstring FreshToken() override { return L"token-" + std::to_wstring(++sequence); }
    std::unique_ptr<OfflineGuard> LockMaintenance(std::wstring&) override { return lock ? std::make_unique<Guard>(lock) : nullptr; }
    bool Docker(DockerState& out, std::wstring&) override { out = docker; return true; }
    bool ScopedCleanup(ObjectKind kind, unsigned age, std::wstring& error) override {
        commands.push_back(CleanupArguments(kind, age)); effects.push_back(L"prune:" + commands.back()[0]);
        if (!prune_ok) error = L"Injected daemon refusal; no retry";
        return prune_ok;
    }
    bool Wsl(const std::wstring&, WslState& out, std::wstring&) override { out = wsl; return true; }
    bool AptClean(const std::wstring& name, std::wstring& error) override {
        effects.push_back(L"apt:" + name); if (!apt_ok) error = L"Apt unavailable"; return apt_ok;
    }
    bool Administrator() override { return admin; }
    bool Target(const std::wstring& path, OfflineTarget& out, std::wstring&) override { target_seen = path; out = target; return true; }
    bool BackupDestination(const std::wstring&, const OfflineTarget&, std::uint64_t, std::wstring&) override { return destination; }
    bool CanStopDocker(std::wstring&) override { return stop_supported; }
    bool StopDocker(std::wstring&) override { effects.push_back(L"stop-docker"); return stop_ok; }
    bool ShutdownWsl(std::wstring&) override { effects.push_back(L"shutdown-wsl"); return shutdown_ok; }
    std::unique_ptr<OfflineGuard> LockOffline(const std::wstring&, const OfflineTarget&, std::wstring&) override {
        effects.push_back(L"exclusive-source"); return offline ? std::make_unique<Guard>(offline) : nullptr;
    }
    bool Backup(const std::wstring& path, const std::wstring& directory, OfflineGuard&, std::wstring&) override {
        backup_source_seen = path; backup_directory_seen = directory;
        effects.push_back(L"backup-sha256-manifest"); if (lose_guard_after_backup) offline = false; return backup_ok;
    }
    bool Journal(const std::wstring&, const std::wstring& step, std::wstring&) override { effects.push_back(L"journal:" + step); return journal_ok; }
    bool Compact(const std::wstring& path, OfflineGuard&, std::wstring& error) override {
        compact_seen = path; effects.push_back(L"compact-handoff");
        if (!native_status_contract) return compact_ok;
        const auto result = CompactStatus(compact_status);
        error = result.message;
        return result.success;
    }
};
Config Settings() { Config c; c.clean_build_cache = c.clean_dangling_images = c.clean_disposable_containers = true; return c; }
Plan Prepare(Engine& engine, Action action, const Config& config = Settings()) {
    Plan plan; std::wstring error;
    Require(engine.Prepare(action, action == Action::Compact ? L"C:\\vhd\\disk.vhdx" : L"Ubuntu", config,
        action == Action::Compact ? L"D:\\backups" : L"", plan, error), "Prepare must succeed");
    Require(error.empty(), "success clears diagnostic"); return plan;
}
Approval Approve(const Plan& plan) { return {plan.token, plan.action == Action::Compact ? L"COMPACT" : L"CLEAN", plan.needs_shutdown}; }
bool Has(const Fake& f, const std::wstring& event) { return std::find(f.effects.begin(), f.effects.end(), event) != f.effects.end(); }
bool StepContains(const Plan& p, const std::wstring& text) {
    return std::any_of(p.steps.begin(), p.steps.end(), [&](const auto& step) { return step.find(text) != std::wstring::npos; });
}
void Quotes() {
    Require(QuoteArgument(L"") == L"\"\"", "empty");
    Require(QuoteArgument(L"abc") == L"\"abc\"", "always quote");
    Require(QuoteArgument(L"a b") == L"\"a b\"", "spaces");
    Require(QuoteArgument(L"a\"b") == L"\"a\\\"b\"", "embedded quote");
    Require(QuoteArgument(L"C:\\a b\\") == L"\"C:\\a b\\\\\"", "trailing slashes");
    Require(QuoteArgument(L"a\\\"b") == L"\"a\\\\\\\"b\"", "slashes before quote");
    Require(QuoteArgument(L"a&b|c%PATH%") == L"\"a&b|c%PATH%\"", "no shell");
    bool rejected = false;
    try { (void)QuoteArgument(std::wstring(L"a\0b", 3)); } catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected, "NUL rejected");
}
void CommandVectors() {
    Require(CleanupArguments(ObjectKind::Image, 30) == std::vector<std::wstring>{L"image", L"prune", L"--force", L"--filter", L"until=720h"}, "dangling-only image prune");
    Require(CleanupArguments(ObjectKind::BuildCache, 30) == std::vector<std::wstring>{L"builder", L"prune", L"--force", L"--filter", L"until=720h"}, "unused cache prune");
    Require(CleanupArguments(ObjectKind::Container, 30) == std::vector<std::wstring>{L"container", L"prune", L"--force", L"--filter", L"label=wsl-cleaner.disposable=true", L"--filter", L"until=720h"}, "exact label and age");
    Require(CleanupArguments(ObjectKind::Image, 0).back() == L"until=0h", "explicit zero age");
    Require(CleanupArguments(ObjectKind::BuildCache, 36500).back() == L"until=876000h", "upper age boundary");
    for (auto kind : {ObjectKind::Container, ObjectKind::Image, ObjectKind::BuildCache}) {
        const auto args = CleanupArguments(kind, 30);
        for (const auto& arg : args) Require(arg != L"system" && arg != L"volume" && arg != L"--volumes" && arg != L"--all" && arg != L"-a", "no broad category/flag");
    }
    bool rejected = false;
    try { (void)CleanupArguments(ObjectKind::Image, 36501); } catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected, "invalid age rejected");
    Require(AptArguments(L"Ubuntu & x") == std::vector<std::wstring>{L"--distribution", L"Ubuntu & x", L"--user", L"root", L"--exec", L"/usr/bin/apt-get", L"clean"}, "apt has a literal distro argument, fixed executable, no shell");
}
void Issuance() {
    Fake f; Engine e(f); Config disabled;
    auto first = Prepare(e, Action::DockerCleanup, disabled); auto second = Prepare(e, Action::DockerCleanup, disabled);
    Require(!first.token.empty() && first.token != second.token && first.expires == f.now + 300, "fresh five-minute token");
    Require(f.effects.empty(), "prepare has no mutations");
    Require(e.Execute(first, Approve(first), disabled).success, "issued no-op succeeds");
    Require(!e.Execute(first, Approve(first), disabled).success, "one-shot");
    f.sequence = 0; Plan p; std::wstring error;
    Require(!e.Prepare(Action::DockerCleanup, L"", disabled, L"", p, error) && p.token.empty(), "consumed token cannot reissue");
}
void Tampering() {
    for (unsigned n = 0; n != 10; ++n) {
        Fake f; Engine e(f); auto p = Prepare(e, Action::DockerCleanup); auto a = Approve(p);
        switch (n) {
        case 0: p.target += L"x"; break;
        case 1: p.backup_directory += L"x"; break;
        case 2: p.action = Action::Compact; break;
        case 3: p.steps.push_back(L"x"); break;
        case 4: ++p.expires; break;
        case 5: p.needs_shutdown = !p.needs_shutdown; break;
        case 6: p.needs_admin = !p.needs_admin; break;
        case 7: a.token += L"x"; break;
        case 8: a.text.clear(); break;
        default: p.token += L"x";
        }
        Require(!e.Execute(p, a, Settings()).success && f.effects.empty() && f.commands.empty(), "tampered approval/plan has no effects");
    }
    Fake f; Engine e(f); Plan p; p.token = L"token-1";
    Require(!e.Execute(p, Approve(p), Settings()).success && f.effects.empty(), "forged plan");
}
void ExpirySettingsLock() {
    for (unsigned n = 0; n != 5; ++n) {
        Fake f; Engine e(f); auto c = Settings(); auto p = Prepare(e, Action::DockerCleanup, c);
        if (n == 0) f.now = p.expires;
        if (n == 1) ++c.minimum_age_days;
        if (n == 2) c.warning_percent = 101;
        if (n == 3) f.lock = false;
        if (n == 4) --f.now;
        Require(!e.Execute(p, Approve(p), c).success && f.effects.empty() && f.commands.empty(), "expiry/settings/lock/rollback no mutation");
    }
    Fake f; Engine e(f); Config c; c.critical_percent = 0; Plan p; std::wstring error;
    Require(!e.Prepare(Action::DockerCleanup, L"", c, L"", p, error) && !error.empty() && p.token.empty(), "invalid settings cannot issue");
}
void OptIns() {
    for (unsigned mask = 0; mask != 8; ++mask) {
        Fake f; Engine e(f); Config c;
        c.clean_disposable_containers = (mask & 1) != 0; c.clean_dangling_images = (mask & 2) != 0; c.clean_build_cache = (mask & 4) != 0;
        auto p = Prepare(e, Action::DockerCleanup, c);
        Require(StepContains(p, L"IRREVERSIBLE"), "preview irreversibility");
        Require(e.Execute(p, Approve(p), c).success, "scoped cleanup");
        Require(Has(f, L"prune:container") == c.clean_disposable_containers, "container opt-in");
        Require(Has(f, L"prune:image") == c.clean_dangling_images, "image opt-in");
        Require(Has(f, L"prune:builder") == c.clean_build_cache, "cache opt-in");
        for (const auto& event : {L"prune:container", L"prune:image", L"prune:builder"}) {
            const auto it = std::find(f.effects.begin(), f.effects.end(), event);
            if (it != f.effects.end()) Require(it != f.effects.begin() && (it - 1)->find(L"journal:pending-prune-") == 0, "journal immediately before prune");
        }
    }
}
void DockerFailures() {
    for (unsigned n = 0; n != 5; ++n) {
        Fake f; Engine e(f); auto p = Prepare(e, Action::DockerCleanup);
        if (n == 0) f.docker.remote_environment = true;
        if (n == 1) f.docker.local_context = false;
        if (n == 2) f.docker.local_engine = false;
        if (n == 3) f.journal_ok = false;
        if (n == 4) f.prune_ok = false;
        const auto r = e.Execute(p, Approve(p), Settings());
        Require(!r.success && !r.message.empty(), "honest daemon/guard refusal");
        Require(f.commands.size() == (n == 4 ? 1U : 0U), "no mutation after refusal and no retry");
    }
}
void ManualApt() {
    Fake f; Engine e(f); auto p = Prepare(e, Action::WslAptCache);
    Require(StepContains(p, L"may start") && StepContains(p, L"Ubuntu"), "named manual-start disclosure");
    f.wsl.running = false;
    Require(e.Execute(p, Approve(p), Settings()).success && Has(f, L"apt:Ubuntu"), "CLEAN permits named stopped distro start");
    for (unsigned n = 0; n != 4; ++n) {
        Fake denied; Engine other(denied); auto q = Prepare(other, Action::WslAptCache); auto a = Approve(q);
        if (n == 0) denied.wsl.registered = false;
        if (n == 1) denied.wsl.docker_owned = true;
        if (n == 2) a.text.clear();
        if (n == 3) denied.journal_ok = false;
        Require(!other.Execute(q, a, Settings()).success && !Has(denied, L"apt:Ubuntu"), "no unapproved/unregistered/Docker apt");
    }
    Fake automatic; Engine other(automatic); Config c = Settings(); c.auto_cleanup = true; Plan q; std::wstring error;
    Require(!other.Prepare(Action::WslAptCache, L"Ubuntu", c, L"", q, error) && automatic.effects.empty(), "automation never starts WSL");
}
void CompactOrdering() {
    Fake f; Engine e(f); auto p = Prepare(e, Action::Compact);
    Require(p.needs_admin && p.needs_shutdown && CompactApproval(p) == L"COMPACT", "admin and explicit global-shutdown approval");
    Require(StepContains(p, L"ALL WSL") && StepContains(p, L"Do NOT restart") && StepContains(p, L"handoff"), "shutdown and residual gap disclosed");
    Require(StepContains(p, L"non-preemptible") && StepContains(p, L"no hard timeout or cancellation"), "native API lifetime disclosed before approval");
    Require(e.Execute(p, Approve(p), Settings()).success, "guarded compact");
    std::vector<std::wstring> operations;
    for (const auto& effect : f.effects) if (effect.find(L"journal:") != 0) operations.push_back(effect);
    Require(operations == std::vector<std::wstring>{L"stop-docker", L"shutdown-wsl", L"exclusive-source", L"backup-sha256-manifest", L"compact-handoff"}, "safe ordering");
    for (const auto* effect : {L"stop-docker", L"shutdown-wsl", L"backup-sha256-manifest", L"compact-handoff"}) {
        const auto it = std::find(f.effects.begin(), f.effects.end(), effect);
        Require(it != f.effects.begin() && (it - 1)->find(L"journal:pending-") == 0, "journal before step");
    }
    Require(Has(f, L"journal:plan-target=" + p.target) && Has(f, L"journal:plan-backup=" + p.backup_directory), "journal binds target and backup before operations");
    Require(Has(f, L"journal:transaction-complete"), "durable complete marker");
}
void CompactFailures() {
    for (unsigned n = 0; n != 15; ++n) {
        Fake f; Engine e(f); auto p = Prepare(e, Action::Compact); auto a = Approve(p);
        if (n == 0) a.allow_shutdown = false;
        if (n == 1) a.text = L"COMPACT wrong-target";
        if (n == 2) f.admin = false;
        if (n == 3) f.docker.any_running_containers = true;
        if (n == 4) f.target.reparse = true;
        if (n == 5) f.target.identity = L"replaced";
        if (n == 6) f.destination = false;
        if (n == 7) f.offline = false;
        if (n == 8) f.backup_ok = false;
        if (n == 9) f.journal_ok = false;
        if (n == 10) f.target.fixed_volume = false;
        if (n == 11) f.lose_guard_after_backup = true;
        if (n == 12) f.stop_supported = false;
        if (n == 13) f.stop_ok = false;
        if (n == 14) f.shutdown_ok = false;
        Require(!e.Execute(p, a, Settings()).success && !Has(f, L"compact-handoff"), "unsafe compact fails closed");
        if (n <= 6 || n == 10 || n == 12 || n == 13) Require(!Has(f, L"shutdown-wsl"), "preflight refusal before shutdown");
        Require(std::count(f.effects.begin(), f.effects.end(), L"backup-sha256-manifest") <= 1, "no backup retry");
        Require(!Has(f, L"journal:transaction-complete"), "interrupted never complete");
    }
}
void HonestCompactorRefusal() {
    Fake f; Engine e(f); auto p = Prepare(e, Action::Compact); f.compact_ok = false;
    const auto result = e.Execute(p, Approve(p), Settings());
    Require(!result.success && result.message.find(L"completed 3") != std::wstring::npos, "partial operation count");
    Require(Has(f, L"backup-sha256-manifest") && !Has(f, L"journal:transaction-complete"), "backup and interrupted status retained");
    Require(!e.Execute(p, Approve(p), Settings()).success && std::count(f.effects.begin(), f.effects.end(), L"compact-handoff") == 1, "no compactor retry");
}
void NativeStatusSuccess() {
    const auto mapped = CompactStatus(0);
    Require(mapped.success && mapped.message.empty(), "DWORD ERROR_SUCCESS must succeed without an error diagnostic");
    Fake f; f.native_status_contract = true; Engine e(f); auto p = Prepare(e, Action::Compact);
    const auto result = e.Execute(p, Approve(p), Settings());
    Require(result.success, "zero native status permits completion after backup and guards");
    Require(Has(f, L"backup-sha256-manifest") && Has(f, L"journal:done-compact") &&
        Has(f, L"journal:transaction-complete"), "successful native status preserves backup-before-complete ordering");
    Require(std::count(f.effects.begin(), f.effects.end(), L"compact-handoff") == 1, "one native attempt");
}
void NativeStatusFailures() {
    for (const std::uint32_t status : {5U, 32U, 5023U, 997U, 0xffffffffU}) {
        const auto expected = L"CompactVirtualDisk failed (DWORD " + std::to_wstring(status) +
            L"); backup retained, no retry. Operation may be partial.";
        const auto mapped = CompactStatus(status);
        Require(!mapped.success && mapped.message == expected, "every nonzero DWORD must fail with the exact unsigned numeric diagnostic");
        Fake f; f.native_status_contract = true; f.compact_status = status; Engine e(f);
        auto p = Prepare(e, Action::Compact);
        const auto result = e.Execute(p, Approve(p), Settings());
        Require(!result.success && result.message.find(expected) != std::wstring::npos &&
            result.message.find(L"completed 3") != std::wstring::npos, "native failure is an honest partial result");
        Require(Has(f, L"backup-sha256-manifest") && Has(f, L"journal:pending-compact") &&
            !Has(f, L"journal:done-compact") && !Has(f, L"journal:transaction-complete"), "native refusal retains backup and interrupted journal");
        const auto effects = f.effects;
        Require(!e.Execute(p, Approve(p), Settings()).success && f.effects == effects &&
            std::count(f.effects.begin(), f.effects.end(), L"compact-handoff") == 1, "nonzero native status is never retried or replayed");
    }
}
void UnicodeCompactPolicy() {
    Fake f; f.native_status_contract = true; Engine e(f); Plan p; std::wstring error;
    const std::wstring path = L"C:\\Usu\u00e1rios\\Jo\u00e3o\\disco.vhdx";
    const std::wstring directory = L"D:\\C\u00f3pias";
    Require(e.Prepare(Action::Compact, path, Settings(), directory, p, error) && error.empty(), "policy accepts Unicode source and backup paths");
    Require(p.target == path && p.backup_directory == directory && StepContains(p, path) && StepContains(p, directory), "Unicode paths survive issuer binding and preview unchanged");
    Require(e.Execute(p, Approve(p), Settings()).success, "Unicode policy flow succeeds with a fake compactor");
    Require(f.target_seen == path && f.backup_source_seen == path && f.backup_directory_seen == directory &&
        f.compact_seen == path, "no Unicode narrowing at target, backup or compactor adapter boundaries");
    Require(Has(f, L"journal:plan-target=" + path) && Has(f, L"journal:plan-backup=" + directory), "journal retains exact Unicode identity strings");
    std::vector<std::wstring> operations;
    for (const auto& event : f.effects) if (event.find(L"journal:") != 0) operations.push_back(event);
    Require(operations == std::vector<std::wstring>{L"stop-docker", L"shutdown-wsl", L"exclusive-source", L"backup-sha256-manifest", L"compact-handoff"}, "Unicode support does not weaken existing safety ordering");
}
} // namespace
int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {{"quoting", Quotes}, {"exact-scoped-vectors", CommandVectors}, {"issuance", Issuance},
        {"tampering", Tampering}, {"expiry-settings-lock", ExpirySettingsLock}, {"opt-ins", OptIns},
        {"docker-refusals", DockerFailures}, {"manual-apt", ManualApt}, {"compact-ordering", CompactOrdering},
        {"compact-refusals", CompactFailures}, {"honest-compactor-refusal", HonestCompactorRefusal},
        {"native-status-success", NativeStatusSuccess}, {"native-status-failures", NativeStatusFailures},
        {"unicode-compact-policy", UnicodeCompactPolicy}};
    unsigned failed = 0;
    for (const auto& test : tests) {
        try { test.run(); std::cout << "PASS " << test.name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.name << ": " << error.what() << '\n'; }
    }
    constexpr auto count = sizeof(tests) / sizeof(tests[0]);
    std::cout << count - failed << '/' << count << " checks passed\n";
    return failed ? 1 : 0;
}
