#include "wsl_cleaner.hpp"
#include <algorithm>
#include <limits>
#include <locale>
#include <sstream>

namespace wc {
namespace {
// ceil(bytes * percent / 100), without forming an overflowing product.
std::uint64_t Percent(std::uint64_t bytes, unsigned percent) {
    return (bytes / 100) * percent + ((bytes % 100) * percent + 99) / 100;
}
std::uint64_t RecoveryReserve(std::uint64_t reserve) {
    const auto margin = reserve / 10 + (reserve % 10 != 0 ? 1 : 0);
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return reserve > maximum - margin ? maximum : reserve + margin;
}
std::wstring Bytes(std::uint64_t value) { return std::to_wstring(value) + L" B"; }
} // namespace

bool ValidateConfig(const Config& config, std::wstring& error) {
    error.clear();
    const auto fail = [&](const wchar_t* message) { error = message; return false; };
    if (config.poll_seconds < 10 || config.poll_seconds > 86400)
        return fail(L"poll_seconds must be between 10 and 86400.");
    if (config.warning_percent < 1 || config.warning_percent >= config.critical_percent ||
        config.critical_percent > 100)
        return fail(L"Thresholds must satisfy 1 <= warning_percent < critical_percent <= 100.");
    if (config.cooldown_seconds > 604800)
        return fail(L"cooldown_seconds must be between 0 and 604800.");
    if (config.minimum_age_days > 36500)
        return fail(L"minimum_age_days must be between 0 and 36500.");
    // Validate all identities before values so the documented diagnostic order
    // does not depend on which map entry has a zero budget.
    for (const auto& budget : config.budgets) {
        const auto& id = budget.first;
        if (id.empty() || id.size() > 32768 || id.find_first_of(L"\r\n=") != std::wstring::npos ||
            id.find(L'\0') != std::wstring::npos)
            return fail(L"Budget IDs must contain 1 to 32768 characters without NUL, CR, LF or '='.");
    }
    for (const auto& budget : config.budgets)
        if (!budget.second) return fail(L"Budget values must be greater than zero.");
    return true;
}

std::wstring FormatSnapshot(const Snapshot& snapshot, const Config& config) {
    std::wostringstream text;
    text.imbue(std::locale::classic());
    text << L"Observed: " << (snapshot.observed ? std::to_wstring(snapshot.observed) : L"unknown") << L'\n';
    for (const auto& host : snapshot.hosts) {
        const bool known = host.total != 0 && host.free <= host.total;
        text << L"Host " << host.root << L" | capacity=" << (known ? Bytes(host.total) : L"unknown")
             << L" | free=" << (known ? Bytes(host.free) : L"unknown") << L'\n';
    }
    for (const auto& disk : snapshot.disks) {
        text << L"Disk " << disk.name << L" | kind=" << disk.kind << L" | path=" << disk.path
             << L" | logical=" << (disk.measured ? Bytes(disk.length) : L"unknown") << L" | budget=";
        const auto budget = config.budgets.find(disk.id);
        if (budget == config.budgets.end()) text << L"unset";
        else text << Bytes(budget->second) << L" (advisory)";
        text << L'\n';
    }
    for (const auto& note : snapshot.notes) text << L"Note: " << note << L'\n';
    return text.str();
}

std::vector<Alert> AlertEngine::Evaluate(const Snapshot& snapshot, const Config& config,
                                       std::uint64_t now) {
    std::vector<Alert> alerts;
    std::wstring error;
    if (!ValidateConfig(config, error) || !snapshot.observed || snapshot.observed > now ||
        now - snapshot.observed > static_cast<std::uint64_t>(config.poll_seconds) * 2)
        return alerts;

    // State changes happen only for known, fresh observations. last is the time
    // of an emitted alert, not the last polling time. Recovery resets both fields.
    const auto notify = [&](const std::wstring& id, unsigned level, const std::wstring& message) {
        auto& state = states_[id];
        const unsigned previous = state.level;
        if (!level) { state = {}; return; }
        state.level = level;
        if (level > previous || (now >= state.last && now - state.last >= config.cooldown_seconds)) {
            alerts.push_back({id, message, level == 2});
            state.last = now;
        }
    };
    const auto previousLevel = [&](const std::wstring& id) {
        const auto found = states_.find(id);
        return found == states_.end() ? 0U : found->second.level;
    };
    if (config.host_reserve) {
        const auto recovery = RecoveryReserve(config.host_reserve);
        for (const auto& host : snapshot.hosts) {
            if (!host.total || host.free > host.total) continue;
            const auto id = L"host:" + host.root;
            unsigned level = host.free <= config.host_reserve / 2 ? 2U :
                             host.free < config.host_reserve ? 1U : 0U;
            if (host.free <= recovery) level = std::max(level, previousLevel(id));
            const auto message = std::wstring(L"Host reserve ") + (level == 2 ? L"critical: " : L"warning: ") +
                host.root + L" (" + Bytes(host.free) + L" free; reserve " + Bytes(config.host_reserve) + L").";
            notify(id, level, message);
        }
    }
    for (const auto& disk : snapshot.disks) {
        const auto budget = config.budgets.find(disk.id);
        if (!disk.measured || budget == config.budgets.end()) continue;
        const auto id = L"budget:" + disk.id;
        const auto bytes = budget->second;
        unsigned level = disk.length >= Percent(bytes, config.critical_percent) ? 2U :
                         disk.length >= Percent(bytes, config.warning_percent) ? 1U : 0U;
        const auto previous = previousLevel(id);
        const auto warning_recovery = config.warning_percent > 5 ? config.warning_percent - 5 : 1U;
        const auto critical_recovery = std::max(config.warning_percent,
            config.critical_percent > 5 ? config.critical_percent - 5 : 1U);
        if (previous == 2 && disk.length >= Percent(bytes, critical_recovery)) level = 2;
        else if (previous && disk.length >= Percent(bytes, warning_recovery)) level = std::max(level, 1U);
        const auto message = std::wstring(L"Budget ") + (level == 2 ? L"critical: " : L"warning: ") +
            disk.name + L" (" + std::to_wstring(disk.length) + L" / " + Bytes(bytes) + L").";
        notify(id, level, message);
    }
    return alerts;
}
} // namespace wc
