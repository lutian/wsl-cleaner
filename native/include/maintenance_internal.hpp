#pragma once
#include "wsl_cleaner.hpp"
#include <memory>
#include <mutex>
#include <set>

// Private, compile-time test seam, never selected by environment/configuration.
namespace wc::maintenance {
constexpr std::uint64_t PlanLifetimeSeconds = 300;
enum class ObjectKind { Container, Image, BuildCache };
struct DockerState {
    bool local_engine = false, local_context = false, remote_environment = false;
    bool any_running_containers = false;
};
struct WslState { bool registered = false, running = false, docker_owned = false; };
struct OfflineTarget {
    std::wstring identity, volume;
    std::uint64_t bytes = 0;
    bool fixed_volume = false, reparse = true, detached = false;
};
// Holds actual exclusive source access during backup. Compact explicitly releases
// that file handle for the final official API handoff; it must not claim otherwise.
struct OfflineGuard {
    virtual ~OfflineGuard() = default;
    virtual bool Verify(std::wstring& error) = 0;
};
// Fixed daemon-side predicates, --force suppresses prompts, never adds --all/-a.
std::vector<std::wstring> CleanupArguments(ObjectKind, unsigned minimum_age_days);
std::vector<std::wstring> AptArguments(const std::wstring& registered_name);
// Locale-independent DWORD result contract shared by the native API and fakes.
Result CompactStatus(std::uint32_t status);
class Adapter {
public:
    virtual ~Adapter() = default;
    virtual std::uint64_t Clock() = 0;
    virtual std::wstring FreshToken() = 0;
    virtual std::unique_ptr<OfflineGuard> LockMaintenance(std::wstring& error) = 0;
    virtual bool Docker(DockerState&, std::wstring& error) = 0;
    virtual bool ScopedCleanup(ObjectKind, unsigned minimum_age_days, std::wstring& error) = 0;
    virtual bool Wsl(const std::wstring& registered_name, WslState&, std::wstring& error) = 0;
    virtual bool AptClean(const std::wstring& registered_name, std::wstring& error) = 0;
    virtual bool Administrator() = 0;
    virtual bool Target(const std::wstring& path, OfflineTarget&, std::wstring& error) = 0;
    virtual bool BackupDestination(const std::wstring& directory, const OfflineTarget&,
        std::uint64_t reserve, std::wstring& error) = 0;
    virtual bool CanStopDocker(std::wstring& error) = 0;
    virtual bool StopDocker(std::wstring& error) = 0;
    virtual bool ShutdownWsl(std::wstring& error) = 0;
    virtual std::unique_ptr<OfflineGuard> LockOffline(const std::wstring& path,
        const OfflineTarget&, std::wstring& error) = 0;
    // Non-overwritten copy, independently verified SHA256 and durable manifest.
    // Failure must never delete, restore, overwrite or retry a backup.
    virtual bool Backup(const std::wstring& path, const std::wstring& directory,
        OfflineGuard&, std::wstring& error) = 0;
    virtual bool Journal(const std::wstring& token, const std::wstring& step,
        std::wstring& error) = 0;
    virtual bool Compact(const std::wstring& path, OfflineGuard&, std::wstring& error) = 0;
};
class Engine {
    struct Issued { Plan plan; Config config; std::uint64_t created; OfflineTarget target; };
    Adapter& adapter_;
    std::map<std::wstring, Issued> issued_;
    std::set<std::wstring> tokens_;
    std::mutex mutex_;
public:
    explicit Engine(Adapter& adapter);
    bool Prepare(Action, const std::wstring&, const Config&, const std::wstring&, Plan&, std::wstring& error);
    Result Execute(const Plan&, const Approval&, const Config&);
};
std::wstring CompactApproval(const Plan&);
} // namespace wc::maintenance
