#include "wsl_cleaner.hpp"
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
wc::Snapshot Sample(std::uint64_t length = 80, std::uint64_t observed = 1000) {
    wc::Snapshot s;
    s.observed = observed;
    s.disks.push_back({L"c:\\wsl\\ext4.vhdx", L"Ubuntu", L"WSL",
                       L"C:\\WSL\\ext4.vhdx", length, true});
    return s;
}
wc::Config Budget(std::uint64_t bytes = 100) {
    wc::Config c;
    c.budgets[L"c:\\wsl\\ext4.vhdx"] = bytes;
    return c;
}
void One(const std::vector<wc::Alert>& alerts, const std::wstring& id,
         const std::wstring& message, bool critical) {
    Require(alerts.size() == 1, "expected exactly one alert");
    Require(alerts[0].id == id, "alert identity differs");
    Require(alerts[0].message == message, "alert message differs");
    Require(alerts[0].critical == critical, "alert severity differs");
}
void BudgetAlert(const std::vector<wc::Alert>& alerts, bool critical,
                 std::uint64_t length, std::uint64_t budget = 100) {
    One(alerts, L"budget:c:\\wsl\\ext4.vhdx",
        std::wstring(L"Budget ") + (critical ? L"critical: " : L"warning: ") +
        L"Ubuntu (" + std::to_wstring(length) + L" / " +
        std::to_wstring(budget) + L" B).", critical);
}
void Invalid(const wc::Config& c, const std::wstring& expected) {
    std::wstring error = L"old error";
    Require(!wc::ValidateConfig(c, error), "invalid configuration accepted");
    Require(error == expected, "configuration diagnostic differs");
}
#ifdef _WIN32
bool Same(const wc::Config& a, const wc::Config& b) {
    return a.poll_seconds == b.poll_seconds && a.warning_percent == b.warning_percent &&
        a.critical_percent == b.critical_percent && a.cooldown_seconds == b.cooldown_seconds &&
        a.minimum_age_days == b.minimum_age_days && a.host_reserve == b.host_reserve &&
        a.auto_cleanup == b.auto_cleanup && a.clean_build_cache == b.clean_build_cache &&
        a.clean_dangling_images == b.clean_dangling_images &&
        a.clean_disposable_containers == b.clean_disposable_containers && a.budgets == b.budgets;
}
// No Collect(), DataDirectory(), processes, registry writes or actual host scans.
// Each filesystem check uses a unique directory beneath the Windows temp root.
struct Scratch {
    std::filesystem::path directory;
    Scratch() {
        wchar_t root[MAX_PATH + 1] = {};
        const DWORD length = GetTempPathW(MAX_PATH + 1, root);
        Require(length != 0 && length <= MAX_PATH, "GetTempPath failed or exceeded buffer");
        for (unsigned n = 0; n != 100; ++n) {
            directory = std::filesystem::path(root) /
                (L"wsl-cleaner-core-checks-" + std::to_wstring(GetCurrentProcessId()) +
                 L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(n));
            if (CreateDirectoryW(directory.c_str(), nullptr)) return;
            Require(GetLastError() == ERROR_ALREADY_EXISTS, "cannot create test directory");
        }
        throw std::runtime_error("cannot allocate unique test directory");
    }
    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void Put(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Require(static_cast<bool>(out), "cannot write test fixture");
}
#endif
} // namespace

int main() {
    unsigned passed = 0, failed = 0;
    const auto test = [&](const char* name, const std::function<void()>& body) {
        try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) {
            ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    };
    test("defaults and valid boundary settings", [] {
        wc::Config c;
        Require(c.poll_seconds == 120 && c.warning_percent == 80 &&
                c.critical_percent == 90 && c.cooldown_seconds == 1800 &&
                c.host_reserve == 20ULL * 1024 * 1024 * 1024,
                "unsafe polling/threshold defaults");
        Require(!c.auto_cleanup && !c.clean_build_cache && !c.clean_dangling_images &&
                !c.clean_disposable_containers && c.budgets.empty(), "unsafe cleanup defaults");
        std::wstring error = L"old error";
        Require(wc::ValidateConfig(c, error) && error.empty(), "defaults rejected");
        c.poll_seconds = 10; c.warning_percent = 1; c.critical_percent = 2;
        c.cooldown_seconds = 0; c.minimum_age_days = 0; c.host_reserve = 0;
        Require(wc::ValidateConfig(c, error) && error.empty(), "lower bounds rejected");
        c.poll_seconds = 86400; c.warning_percent = 99; c.critical_percent = 100;
        c.cooldown_seconds = 604800; c.minimum_age_days = 36500;
        c.host_reserve = std::numeric_limits<std::uint64_t>::max();
        c.budgets[L"c:\\data\\ext4.vhdx"] = c.host_reserve;
        Require(wc::ValidateConfig(c, error) && error.empty(), "upper bounds rejected");
    });
    test("reject each invalid setting with exact diagnostics", [] {
        for (unsigned n : {0U, 9U, 86401U}) {
            wc::Config c; c.poll_seconds = n;
            Invalid(c, L"poll_seconds must be between 10 and 86400.");
        }
        for (auto pair : std::vector<std::pair<unsigned, unsigned>>{
                {0, 90}, {80, 80}, {90, 80}, {80, 101}}) {
            wc::Config c; c.warning_percent = pair.first; c.critical_percent = pair.second;
            Invalid(c, L"Thresholds must satisfy 1 <= warning_percent < critical_percent <= 100.");
        }
        wc::Config c; c.cooldown_seconds = 604801;
        Invalid(c, L"cooldown_seconds must be between 0 and 604800.");
        c = {}; c.minimum_age_days = 36501;
        Invalid(c, L"minimum_age_days must be between 0 and 36500.");
        c = {}; c.budgets[L"disk"] = 0;
        Invalid(c, L"Budget values must be greater than zero.");
        for (const auto& id : std::vector<std::wstring>{L"", L"a=b", L"a\nb", L"a\rb",
                                                      std::wstring(L"a\0b", 3),
                                                      std::wstring(32769, L'x')}) {
            c = {}; c.budgets[id] = 1;
            Invalid(c, L"Budget IDs must contain 1 to 32768 characters without NUL, CR, LF or '='.");
        }
    });
    test("warning inclusive boundary and advisory-only behavior", [] {
        wc::AlertEngine e; auto c = Budget();
        Require(e.Evaluate(Sample(79), c, 1000).empty(), "premature warning");
        BudgetAlert(e.Evaluate(Sample(80), c, 1000), false, 80);
        Require(c.budgets.at(L"c:\\wsl\\ext4.vhdx") == 100 && !c.auto_cleanup,
                "evaluation mutated configuration");
    });
    test("critical boundary and cooldown escalation", [] {
        wc::AlertEngine e; auto c = Budget();
        BudgetAlert(e.Evaluate(Sample(80), c, 1000), false, 80);
        Require(e.Evaluate(Sample(89, 1001), c, 1001).empty(), "cooldown ignored");
        BudgetAlert(e.Evaluate(Sample(90, 1002), c, 1002), true, 90);
        Require(e.Evaluate(Sample(95, 2801), c, 2801).empty(), "critical cooldown too short");
        BudgetAlert(e.Evaluate(Sample(95, 2802), c, 2802), true, 95);
    });
    test("recovery hysteresis prevents flapping and rearms", [] {
        wc::AlertEngine e; auto c = Budget();
        BudgetAlert(e.Evaluate(Sample(80), c, 1000), false, 80);
        Require(e.Evaluate(Sample(75, 1001), c, 1001).empty(), "hysteresis boundary emitted");
        Require(e.Evaluate(Sample(80, 1002), c, 1002).empty(), "boundary incorrectly rearmed");
        Require(e.Evaluate(Sample(74, 1003), c, 1003).empty(), "recovery emitted warning");
        BudgetAlert(e.Evaluate(Sample(80, 1004), c, 1004), false, 80);
    });
    test("critical hysteresis retains severity until below 85 percent", [] {
        wc::AlertEngine e; auto c = Budget(); c.cooldown_seconds = 0;
        BudgetAlert(e.Evaluate(Sample(90), c, 1000), true, 90);
        BudgetAlert(e.Evaluate(Sample(85, 1001), c, 1001), true, 85);
        BudgetAlert(e.Evaluate(Sample(84, 1002), c, 1002), false, 84);
    });
    test("unknown stale future and absent observations do not clear active state", [] {
        wc::AlertEngine e; auto c = Budget();
        BudgetAlert(e.Evaluate(Sample(80), c, 1000), false, 80);
        auto unknown = Sample(0, 1001); unknown.disks[0].measured = false;
        Require(e.Evaluate(unknown, c, 1001).empty(), "unknown emitted alert");
        Require(e.Evaluate(Sample(0, 1), c, 1002).empty(), "stale emitted alert");
        Require(e.Evaluate(Sample(0, 1004), c, 1003).empty(), "future emitted alert");
        Require(e.Evaluate(Sample(0, 0), c, 1004).empty(), "undated emitted alert");
        wc::Snapshot missing; missing.observed = 1005;
        Require(e.Evaluate(missing, c, 1005).empty(), "missing emitted alert");
        Require(e.Evaluate(Sample(80, 1006), c, 1006).empty(), "unknown state rearmed warning");
    });
    test("freshness inclusive two-poll boundary", [] {
        wc::AlertEngine e; auto c = Budget();
        Require(e.Evaluate(Sample(90, 1000), c, 1241).empty(), "stale sample accepted");
        BudgetAlert(e.Evaluate(Sample(90, 1000), c, 1240), true, 90);
    });
    test("overflow-safe percentages and small-byte rounding", [] {
        auto maximum = std::numeric_limits<std::uint64_t>::max();
        auto c = Budget(maximum); wc::AlertEngine e;
        const auto ceiling80 = (maximum / 100) * 80 + ((maximum % 100) * 80 + 99) / 100;
        Require(e.Evaluate(Sample(ceiling80 - 1), c, 1000).empty(), "overflow or premature rounding");
        BudgetAlert(e.Evaluate(Sample(ceiling80), c, 1000), false, ceiling80, maximum);
        BudgetAlert(e.Evaluate(Sample(maximum, 1001), c, 1001), true, maximum, maximum);
        wc::AlertEngine tiny; c = Budget(1);
        Require(tiny.Evaluate(Sample(0), c, 1000).empty(), "zero bytes reached tiny budget");
        BudgetAlert(tiny.Evaluate(Sample(1), c, 1000), true, 1, 1);
    });
    test("clock rollback and zero cooldown preserve notification timing", [] {
        wc::AlertEngine e; auto c = Budget();
        BudgetAlert(e.Evaluate(Sample(80), c, 1000), false, 80);
        Require(e.Evaluate(Sample(80, 999), c, 999).empty(), "clock rollback underflowed cooldown");
        c.cooldown_seconds = 0;
        BudgetAlert(e.Evaluate(Sample(80, 1001), c, 1001), false, 80);
        BudgetAlert(e.Evaluate(Sample(80, 1001), c, 1001), false, 80);
    });
    test("unknown high length and freshness cannot create a first alarm", [] {
        auto c = Budget();
        auto unknown = Sample(100); unknown.disks[0].measured = false;
        for (const auto& s : std::vector<wc::Snapshot>{unknown, Sample(100, 0),
                                                     Sample(100, 1), Sample(100, 1001)}) {
            wc::AlertEngine e;
            Require(e.Evaluate(s, c, 1000).empty(), "unknown or nonfresh sample alerted");
            BudgetAlert(e.Evaluate(Sample(80), c, 1000), false, 80);
        }
    });
    test("separate stable identities have independent cooldowns", [] {
        auto c = Budget(); c.budgets[L"d:\\data.vhdx"] = 100;
        wc::AlertEngine e;
        BudgetAlert(e.Evaluate(Sample(80), c, 1000), false, 80);
        auto s = Sample(80, 1001);
        s.disks.push_back({L"d:\\data.vhdx", L"Other", L"Docker", L"D:\\data.vhdx", 90, true});
        One(e.Evaluate(s, c, 1001), L"budget:d:\\data.vhdx",
            L"Budget critical: Other (90 / 100 B).", true);
        Require(e.Evaluate(s, c, 1001).empty(), "independent cooldown not recorded");
    });
    test("host recovery arithmetic saturates at uint64 maximum", [] {
        wc::Config c; c.host_reserve = std::numeric_limits<std::uint64_t>::max();
        wc::Snapshot s; s.observed = 1000;
        s.hosts.push_back({L"D:\\", c.host_reserve, c.host_reserve - 1});
        wc::AlertEngine e;
        One(e.Evaluate(s, c, 1000), L"host:D:\\",
            L"Host reserve warning: D:\\ (18446744073709551614 B free; reserve 18446744073709551615 B).", false);
        s.observed = 1001; s.hosts[0].free = c.host_reserve;
        Require(e.Evaluate(s, c, 1001).empty(), "saturated recovery emitted alarm");
        s.observed = 1002; s.hosts[0].free = c.host_reserve - 1;
        Require(e.Evaluate(s, c, 1002).empty(), "overflow incorrectly rearmed host alert");
    });
    test("no configured budget is not a quota", [] {
        wc::AlertEngine e; wc::Config c;
        Require(e.Evaluate(Sample(std::numeric_limits<std::uint64_t>::max()), c, 1000).empty(),
                "unconfigured budget alerted");
    });
    test("host reserve thresholds hysteresis and unknown capacity", [] {
        wc::Config c; c.host_reserve = 20; wc::AlertEngine e;
        wc::Snapshot s; s.observed = 1000; s.hosts.push_back({L"C:\\", 100, 20});
        Require(e.Evaluate(s, c, 1000).empty(), "reserve equality is not low space");
        s.hosts[0].free = 19;
        One(e.Evaluate(s, c, 1000), L"host:C:\\",
            L"Host reserve warning: C:\\ (19 B free; reserve 20 B).", false);
        s.observed = 1001; s.hosts[0].free = 10;
        One(e.Evaluate(s, c, 1001), L"host:C:\\",
            L"Host reserve critical: C:\\ (10 B free; reserve 20 B).", true);
        s.observed = 1002; s.hosts[0] = {L"C:\\", 0, 0};
        Require(e.Evaluate(s, c, 1002).empty(), "unknown capacity alerted");
        s.observed = 1003; s.hosts[0] = {L"C:\\", 100, 22};
        Require(e.Evaluate(s, c, 1003).empty(), "host recovery boundary alerted");
        s.observed = 1004; s.hosts[0].free = 19;
        Require(e.Evaluate(s, c, 1004).empty(), "host boundary incorrectly rearmed");
        s.observed = 1005; s.hosts[0].free = 23;
        Require(e.Evaluate(s, c, 1005).empty(), "host recovery alerted");
        s.observed = 1006; s.hosts[0].free = 19;
        One(e.Evaluate(s, c, 1006), L"host:C:\\",
            L"Host reserve warning: C:\\ (19 B free; reserve 20 B).", false);
    });
    test("zero host reserve disabled and impossible measurements ignored", [] {
        wc::Snapshot s; s.observed = 1000; s.hosts.push_back({L"C:\\", 100, 0});
        wc::Config c; c.host_reserve = 0; wc::AlertEngine e;
        Require(e.Evaluate(s, c, 1000).empty(), "disabled reserve alerted");
        c.host_reserve = 20; s.hosts[0].free = 101;
        Require(e.Evaluate(s, c, 1000).empty(), "impossible host measurement alerted");
    });
    test("invalid config cannot mutate alert state", [] {
        wc::AlertEngine e; auto c = Budget(); auto bad = c; bad.warning_percent = 0;
        Require(e.Evaluate(Sample(90), bad, 1000).empty(), "invalid config alerted");
        BudgetAlert(e.Evaluate(Sample(80), c, 1000), false, 80);
    });
    test("format exposes logical lengths budgets unknowns and notes exactly", [] {
        auto s = Sample(); auto c = Budget();
        s.hosts.push_back({L"C:\\", 1000, 500});
        s.disks.push_back({L"missing", L"Docker data", L"Docker", L"C:\\missing.vhdx", 0, false});
        s.notes.push_back(L"Metadata only; no WSL or Docker process was started.");
        Require(wc::FormatSnapshot(s, c) ==
            L"Observed: 1000\n"
            L"Host C:\\ | capacity=1000 B | free=500 B\n"
            L"Disk Ubuntu | kind=WSL | path=C:\\WSL\\ext4.vhdx | logical=80 B | budget=100 B (advisory)\n"
            L"Disk Docker data | kind=Docker | path=C:\\missing.vhdx | logical=unknown | budget=unset\n"
            L"Note: Metadata only; no WSL or Docker process was started.\n",
            "formatted snapshot hides or misrepresents inventory");
        wc::Snapshot unknown;
        unknown.hosts.push_back({L"D:\\", 0, 0});
        Require(wc::FormatSnapshot(unknown, {}) ==
            L"Observed: unknown\nHost D:\\ | capacity=unknown | free=unknown\n",
            "unknown host represented as zero");
    });
#ifdef _WIN32
    test("isolated config roundtrip is UTF-8 and explicit save only", [] {
        Scratch scratch; auto path = scratch.directory / L"settings.ini";
        wc::Config c; c.budgets[L"c:\\dados\\\u6570\u636e.vhdx"] = 123;
        c.clean_build_cache = true; c.poll_seconds = 600; c.cooldown_seconds = 0;
        c.budgets[L"d:\\maximum.vhdx"] = std::numeric_limits<std::uint64_t>::max();
        std::wstring error = L"old";
        Require(wc::SaveConfig(path.wstring(), c, error) && error.empty(), "save failed");
        const auto before = Bytes(path);
        Require(before.find("version=1\n") == 0 && before.find("\xe6\x95\xb0\xe6\x8d\xae") != std::string::npos,
                "format not versioned UTF-8");
        wc::Config loaded; error = L"old";
        Require(wc::LoadConfig(path.wstring(), loaded, error) && error.empty(), "load failed");
        Require(Same(loaded, c), "roundtrip changed settings");
        Require(Bytes(path) == before, "load rewrote settings");
        c.poll_seconds = 601;
        Require(wc::SaveConfig(path.wstring(), c, error) && error.empty(), "explicit replacement failed");
        Require(wc::LoadConfig(path.wstring(), loaded, error) && loaded.poll_seconds == 601,
                "replacement did not persist new settings");
        Require(std::distance(std::filesystem::directory_iterator(scratch.directory),
                              std::filesystem::directory_iterator{}) == 1, "temporary save file leaked");
    });
    test("corrupt config rejected without file or output mutation", [] {
        Scratch scratch; auto path = scratch.directory / L"settings.ini";
        wc::Config defaults; std::wstring error;
        Require(wc::SaveConfig(path.wstring(), defaults, error), "cannot create valid fixture");
        const auto valid = Bytes(path);
        auto replaced = [&](const std::string& from, const std::string& to) {
            auto text = valid; const auto at = text.find(from);
            Require(at != std::string::npos, "fixture field absent");
            text.replace(at, from.size(), to); return text;
        };
        const std::vector<std::string> corrupt = {
            "", "version=1\n", replaced("version=1", "version=2"),
            std::string("\xef\xbb\xbf") + valid, valid + "\n", valid + "# comment\n",
            replaced("version=1\n", "version=1\r\n"),
            valid + "poll_seconds=120\n", valid + "unknown=1\n",
            replaced("poll_seconds=120", "poll_seconds=-1"),
            replaced("poll_seconds=120", "poll_seconds=120junk"),
            replaced("poll_seconds=120", "poll_seconds=+120"),
            replaced("poll_seconds=120", "poll_seconds= 120"),
            replaced("poll_seconds=120\n", ""),
            replaced("host_reserve=21474836480", "host_reserve=18446744073709551616"),
            replaced("auto_cleanup=0", "auto_cleanup=true"),
            replaced("auto_cleanup=0", "auto_cleanup=2"),
            valid + "budget.disk=0\n", valid + "budget.disk=1\nbudget.disk=2\n",
            valid + std::string("budget.\xff=1\n"), valid + std::string("x\0=1\n", 5)};
        for (const auto& bytes : corrupt) {
            Put(path, bytes); wc::Config out; out.poll_seconds = 777;
            out.budgets[L"sentinel"] = 42; error.clear();
            const auto original = out;
            Require(!wc::LoadConfig(path.wstring(), out, error) && !error.empty(), "corrupt config accepted");
            Require(Same(out, original), "rejection changed caller settings");
            Require(Bytes(path) == bytes, "rejection overwrote corrupt source");
        }
        Put(path, valid); auto invalid = defaults; invalid.poll_seconds = 0;
        Require(!wc::SaveConfig(path.wstring(), invalid, error) && !error.empty(), "invalid save accepted");
        Require(Bytes(path) == valid, "invalid save modified stored data");
        wc::Config out; out.poll_seconds = 777;
        Require(!wc::LoadConfig((scratch.directory / L"missing.ini").wstring(), out, error),
                "missing config silently created");
        Require(out.poll_seconds == 777 && !std::filesystem::exists(scratch.directory / L"missing.ini"),
                "missing config modified state");
    });
#endif
    std::cout << passed << " passed; " << failed << " failed\n";
    return failed ? 1 : 0;
}
