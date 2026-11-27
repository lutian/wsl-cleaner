#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace wc {
struct HostDisk { std::wstring root; std::uint64_t total = 0, free = 0; };
struct VirtualDisk {
    std::wstring id, name, kind, path;
    std::uint64_t length = 0;
    bool measured = false;
};
struct Snapshot {
    std::vector<HostDisk> hosts;
    std::vector<VirtualDisk> disks;
    std::vector<std::wstring> notes;
    std::uint64_t observed = 0;
};
struct Config {
    unsigned poll_seconds = 120, warning_percent = 80, critical_percent = 90;
    unsigned cooldown_seconds = 1800, minimum_age_days = 30;
    std::uint64_t host_reserve = 20ULL * 1024 * 1024 * 1024;
    bool auto_cleanup = false, clean_build_cache = false;
    bool clean_dangling_images = false, clean_disposable_containers = false;
    std::map<std::wstring, std::uint64_t> budgets;
};
struct Alert { std::wstring id, message; bool critical = false; };
bool ValidateConfig(const Config&, std::wstring& error);
bool LoadConfig(const std::wstring& path, Config&, std::wstring& error);
bool SaveConfig(const std::wstring& path, const Config&, std::wstring& error);
std::wstring DataDirectory();
std::uint64_t Now();
Snapshot Collect();
std::wstring FormatSnapshot(const Snapshot&, const Config&);
class AlertEngine {
    struct State { unsigned level = 0; std::uint64_t last = 0; };
    std::map<std::wstring, State> states_;
public:
    std::vector<Alert> Evaluate(const Snapshot&, const Config&, std::uint64_t now);
};
enum class Action { DockerCleanup, WslAptCache, Compact };
struct Plan {
    std::wstring token, target, backup_directory;
    Action action = Action::DockerCleanup;
    std::vector<std::wstring> steps;
    std::uint64_t expires = 0;
    bool needs_shutdown = false, needs_admin = false;
};
struct Approval { std::wstring token, text; bool allow_shutdown = false; };
struct Result { bool success = false; std::wstring message; };
class MaintenanceService {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    MaintenanceService();
    ~MaintenanceService();
    MaintenanceService(const MaintenanceService&) = delete;
    MaintenanceService& operator=(const MaintenanceService&) = delete;
    bool Prepare(Action, const std::wstring& target, const Config&,
        const std::wstring& backup_directory, Plan&, std::wstring& error);
    Result Execute(const Plan&, const Approval&, const Config&);
};
// Exact command-line quoting, shared by production process adapter and public tests.
std::wstring QuoteArgument(const std::wstring&);
} // namespace wc
