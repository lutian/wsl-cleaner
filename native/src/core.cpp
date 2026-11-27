#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "wsl_cleaner.hpp"
#include "host_inventory_internal.hpp"
#include <algorithm>
#include <filesystem>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

namespace wc {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Key {
    HKEY value = nullptr;
    ~Key() { if (value) RegCloseKey(value); }
};
std::wstring Lower(std::wstring text) {
    if (text.empty()) return text;
    const int length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr, 0);
    if (!length) return L"";
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(),
        static_cast<int>(text.size()), &result[0], length, nullptr, nullptr, 0)) return L"";
    return result;
}
// Lexical identity only; never resolves a junction, UNC share or symlink.
std::wstring Normalize(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.compare(0, 4, L"\\\\?\\") == 0) path.erase(0, 4);
    if (path.size() < 3 || path[1] != L':' || path[2] != L'\\' ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        path.find(L':', 2) != std::wstring::npos || path.find(L'\0') != std::wstring::npos)
        return L"";
    DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!size || size > 32768) return L"";
    std::wstring full(size, L'\0');
    DWORD used = GetFullPathNameW(path.c_str(), size, &full[0], nullptr);
    if (!used || used >= size) return L"";
    full.resize(used);
    return Lower(full);
}
bool LocalMetadataPath(const std::wstring& path, bool allow_missing) {
    const auto full = Normalize(path);
    if (full.empty() || GetDriveTypeW(full.substr(0, 3).c_str()) != DRIVE_FIXED) return false;
    // Check every existing component, not merely the leaf. A missing leaf is
    // acceptable for inventory/config creation; an inaccessible one is not.
    for (std::size_t end = 3;;) {
        const auto part = full.substr(0, end);
        const DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            return allow_missing && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
        if (end == full.size()) return true;
        const auto next = full.find(L'\\', end + (end == 3 ? 0 : 1));
        end = next == std::wstring::npos ? full.size() : next;
    }
}
bool Utf8(const std::wstring& text, std::string& result) {
    if (text.empty()) { result.clear(); return true; }
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!count) return false;
    result.resize(count);
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), &result[0], count, nullptr, nullptr) == count;
}
bool Wide(const std::string& text, std::wstring& result) {
    if (text.empty()) { result.clear(); return true; }
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (!count) return false;
    result.resize(count);
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), &result[0], count) == count;
}
bool Number(const std::wstring& value, std::uint64_t& result) {
    if (value.empty()) return false;
    result = 0;
    for (wchar_t ch : value) {
        if (ch < L'0' || ch > L'9') return false;
        const auto digit = static_cast<unsigned>(ch - L'0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        result = result * 10 + digit;
    }
    return true;
}
std::wstring RegistryText(HKEY key, const wchar_t* name, bool* missing = nullptr) {
    DWORD type = 0, bytes = 0;
    const LONG queried = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (missing) *missing = queried == ERROR_FILE_NOT_FOUND;
    if (queried != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
        bytes < sizeof(wchar_t) || bytes > 65536 || bytes % sizeof(wchar_t)) return L"";
    const DWORD expected_type = type, capacity = bytes;
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type,
        reinterpret_cast<BYTE*>(&value[0]), &bytes) != ERROR_SUCCESS || type != expected_type ||
        bytes < sizeof(wchar_t) || bytes > capacity || bytes % sizeof(wchar_t)) return L"";
    value.resize(bytes / sizeof(wchar_t));
    if (value.back() != L'\0') return L"";
    value.pop_back();
    if (value.find(L'\0') != std::wstring::npos) return L"";
    if (type == REG_EXPAND_SZ) {
        const DWORD size = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (!size || size > 32768) return L"";
        std::wstring expanded(size, L'\0');
        if (ExpandEnvironmentStringsW(value.c_str(), &expanded[0], size) != size) return L"";
        expanded.pop_back(); return expanded;
    }
    return value;
}
std::wstring LocalAppData() {
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!size || size > 32768) return L"";
    std::wstring path(size, L'\0');
    DWORD used = GetEnvironmentVariableW(L"LOCALAPPDATA", &path[0], size);
    if (!used || used >= size) return L"";
    path.resize(used);
    return Normalize(path);
}
void AddDisk(Snapshot& s, std::set<std::wstring>& seen, const std::wstring& path,
             const std::wstring& name, const std::wstring& kind) {
    const auto id = Normalize(path);
    if (id.empty()) {
        s.notes.push_back(L"Unknown " + kind + L" disk path for " + name +
                          L" (relative, UNC or unsupported device path).");
        return;
    }
    if (!seen.insert(id).second) return;
    VirtualDisk disk; disk.id = id; disk.path = id; disk.name = name; disk.kind = kind;
    if (LocalMetadataPath(id, false)) {
        Handle file(CreateFileW(id.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        BY_HANDLE_FILE_INFORMATION info = {};
        LARGE_INTEGER size = {};
        if (file.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file.value, &info) &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
            GetFileSizeEx(file.value, &size) && size.QuadPart >= 0) {
            disk.length = static_cast<std::uint64_t>(size.QuadPart);
            disk.measured = true;
        }
    }
    if (!disk.measured) s.notes.push_back(L"Logical length unknown (missing, inaccessible or unsafe path): " + id);
    s.disks.push_back(disk);
}
} // namespace

std::uint64_t Now() {
    FILETIME time = {}; GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER ticks = {}; ticks.LowPart = time.dwLowDateTime; ticks.HighPart = time.dwHighDateTime;
    constexpr std::uint64_t epoch = 116444736000000000ULL;
    return ticks.QuadPart < epoch ? 0 : (ticks.QuadPart - epoch) / 10000000;
}
std::wstring DataDirectory() {
    const auto base = LocalAppData();
    return base.empty() ? L"" : base + L"\\WslCleaner";
}
bool LoadConfig(const std::wstring& path, Config& output, std::wstring& error) {
    error.clear();
    const auto fail = [&](const wchar_t* message) { error = message; return false; };
    if (!LocalMetadataPath(path, false)) return fail(L"Settings path is missing, inaccessible or unsafe.");
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) return fail(L"Cannot open settings.");
    BY_HANDLE_FILE_INFORMATION info = {};
    if (!GetFileInformationByHandle(file.value, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        return fail(L"Settings file is not a regular non-reparse file.");
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024)
        return fail(L"Settings size is invalid.");
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(file.value, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size())
        return fail(L"Cannot read complete settings.");
    std::wstring text;
    if (!Wide(bytes, text) || text.find(L'\0') != std::wstring::npos ||
        text.find(L'\r') != std::wstring::npos || text.empty() || text.back() != L'\n')
        return fail(L"Settings must be strict UTF-8 with LF-terminated lines.");
    Config candidate;
    std::set<std::wstring> fields;
    std::wistringstream lines(text);
    lines.imbue(std::locale::classic());
    std::wstring line;
    while (std::getline(lines, line)) {
        const auto equals = line.find(L'=');
        if (equals == std::wstring::npos || equals == 0) return fail(L"Malformed settings field.");
        const auto key = line.substr(0, equals);
        const auto value = line.substr(equals + 1);
        if (!fields.insert(key).second) return fail(L"Duplicate settings field.");
        std::uint64_t number = 0;
        if (!Number(value, number)) return fail(L"Settings values must be unsigned decimal integers.");
        if (key == L"version") {
            if (number != 1) return fail(L"Unsupported settings version.");
        } else if (key == L"host_reserve") candidate.host_reserve = number;
        else if (key.compare(0, 7, L"budget.") == 0) candidate.budgets.emplace(key.substr(7), number);
        else if (key == L"poll_seconds" || key == L"warning_percent" || key == L"critical_percent" ||
                 key == L"cooldown_seconds" || key == L"minimum_age_days") {
            if (number > std::numeric_limits<unsigned>::max()) return fail(L"Settings integer exceeds supported range.");
            const auto n = static_cast<unsigned>(number);
            if (key == L"poll_seconds") candidate.poll_seconds = n;
            if (key == L"warning_percent") candidate.warning_percent = n;
            if (key == L"critical_percent") candidate.critical_percent = n;
            if (key == L"cooldown_seconds") candidate.cooldown_seconds = n;
            if (key == L"minimum_age_days") candidate.minimum_age_days = n;
        } else if (key == L"auto_cleanup" || key == L"clean_build_cache" ||
                   key == L"clean_dangling_images" || key == L"clean_disposable_containers") {
            if (number > 1) return fail(L"Boolean settings must be 0 or 1.");
            if (key == L"auto_cleanup") candidate.auto_cleanup = number != 0;
            if (key == L"clean_build_cache") candidate.clean_build_cache = number != 0;
            if (key == L"clean_dangling_images") candidate.clean_dangling_images = number != 0;
            if (key == L"clean_disposable_containers") candidate.clean_disposable_containers = number != 0;
        } else return fail(L"Unknown settings field.");
    }
    for (const wchar_t* required : {L"version", L"poll_seconds", L"warning_percent", L"critical_percent",
            L"cooldown_seconds", L"minimum_age_days", L"host_reserve", L"auto_cleanup",
            L"clean_build_cache", L"clean_dangling_images", L"clean_disposable_containers"})
        if (!fields.count(required)) return fail(L"Missing required settings field.");
    if (!ValidateConfig(candidate, error)) return false;
    output = candidate;
    return true;
}
bool SaveConfig(const std::wstring& path, const Config& config, std::wstring& error) {
    error.clear();
    if (!ValidateConfig(config, error)) return false;
    const auto fail = [&](const wchar_t* message) { error = message; return false; };
    const auto normalized = Normalize(path);
    if (normalized.empty() || !LocalMetadataPath(normalized, true)) return fail(L"Settings path is unsafe.");
    std::wostringstream text;
    text.imbue(std::locale::classic());
    text << L"version=1\npoll_seconds=" << config.poll_seconds
         << L"\nwarning_percent=" << config.warning_percent
         << L"\ncritical_percent=" << config.critical_percent
         << L"\ncooldown_seconds=" << config.cooldown_seconds
         << L"\nminimum_age_days=" << config.minimum_age_days
         << L"\nhost_reserve=" << config.host_reserve
         << L"\nauto_cleanup=" << config.auto_cleanup
         << L"\nclean_build_cache=" << config.clean_build_cache
         << L"\nclean_dangling_images=" << config.clean_dangling_images
         << L"\nclean_disposable_containers=" << config.clean_disposable_containers << L'\n';
    for (const auto& entry : config.budgets) text << L"budget." << entry.first << L'=' << entry.second << L'\n';
    std::string bytes;
    if (!Utf8(text.str(), bytes) || bytes.size() > 4 * 1024 * 1024) return fail(L"Settings encoding or size is invalid.");
    const auto parent = std::filesystem::path(normalized).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec || !LocalMetadataPath(parent.wstring(), false)) return fail(L"Cannot safely create settings directory.");
    // CREATE_NEW is the uniqueness authority; no predictable overwrite or shared temp file.
    std::wstring temporary;
    HANDLE raw = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        temporary = normalized + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                    std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
        raw = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
        if (raw != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            return fail(L"Cannot create temporary settings file.");
    }
    if (raw == INVALID_HANDLE_VALUE) return fail(L"Cannot allocate unique temporary settings file.");
    bool written = false;
    {
        Handle file(raw); DWORD count = 0;
        written = WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) &&
                  count == bytes.size() && FlushFileBuffers(file.value);
    }
    if (!written || !LocalMetadataPath(normalized, true) ||
        !MoveFileExW(temporary.c_str(), normalized.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // Only the unique scratch file created by this save is removed.
        DeleteFileW(temporary.c_str());
        return fail(L"Atomic settings save failed; existing settings were not intentionally replaced.");
    }
    return true;
}
Snapshot Collect() {
    Snapshot snapshot;
    std::set<std::wstring> hosts, disks;
    wchar_t volume[32768] = {};
    HANDLE search = FindFirstVolumeW(volume, 32768);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (GetDriveTypeW(volume) != DRIVE_FIXED) continue;
            if (!hosts.insert(Lower(volume)).second) continue;
            internal::CollectMountedHost(snapshot, volume,
                [&](wchar_t* paths, std::size_t size) {
                    DWORD needed = 0;
                    return GetVolumePathNamesForVolumeNameW(volume, paths,
                        static_cast<DWORD>(size), &needed) != FALSE;
                }, [&](std::uint64_t& capacity, std::uint64_t& free_bytes) {
                    ULARGE_INTEGER available = {}, total = {}, free = {};
                    if (!GetDiskFreeSpaceExW(volume, &available, &total, &free)) return false;
                    capacity = total.QuadPart; free_bytes = free.QuadPart;
                    return true;
                });
        } while (FindNextVolumeW(search, volume, 32768));
        if (GetLastError() != ERROR_NO_MORE_FILES) snapshot.notes.push_back(L"Host volume enumeration was incomplete.");
        FindVolumeClose(search);
    } else snapshot.notes.push_back(L"Host volume inventory is unavailable.");
    Key lxss;
    const LONG opened = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Lxss", 0, KEY_READ, &lxss.value);
    if (opened == ERROR_SUCCESS) {
        for (DWORD index = 0;; ++index) {
            wchar_t name[256] = {}; DWORD count = 256;
            LONG result = RegEnumKeyExW(lxss.value, index, name, &count, nullptr, nullptr, nullptr, nullptr);
            if (result == ERROR_NO_MORE_ITEMS) break;
            if (result != ERROR_SUCCESS) { snapshot.notes.push_back(L"WSL registry inventory is incomplete."); break; }
            Key distro;
            if (RegOpenKeyExW(lxss.value, name, 0, KEY_READ, &distro.value) != ERROR_SUCCESS) {
                snapshot.notes.push_back(L"WSL distribution metadata is inaccessible."); continue;
            }
            DWORD type = 0, version = 0, bytes = sizeof(version);
            if (RegQueryValueExW(distro.value, L"Version", nullptr, &type,
                reinterpret_cast<BYTE*>(&version), &bytes) != ERROR_SUCCESS || type != REG_DWORD || bytes != sizeof(version)) {
                snapshot.notes.push_back(L"WSL distribution version is unknown."); continue;
            }
            if (version != 2) continue;
            auto label = RegistryText(distro.value, L"DistributionName");
            if (label.empty()) label = name;
            const auto base = RegistryText(distro.value, L"BasePath");
            bool vhd_missing = false;
            auto vhd = RegistryText(distro.value, L"VhdFileName", &vhd_missing);
            if (vhd_missing) vhd = L"ext4.vhdx";
            std::wstring path;
            if (!Normalize(vhd).empty()) path = vhd;
            else if (!base.empty() && !vhd.empty() && vhd.find_first_of(L"\\/:") == std::wstring::npos)
                path = base + L"\\" + vhd;
            const auto lower = Lower(label);
            const bool docker = lower == L"docker-desktop" || lower == L"docker-desktop-data";
            AddDisk(snapshot, disks, path, label, docker ? L"Docker" : L"WSL");
        }
    } else snapshot.notes.push_back(opened == ERROR_FILE_NOT_FOUND ?
        L"No per-user WSL registry inventory found; this does not prove WSL is uninstalled." :
        L"Per-user WSL registry inventory is inaccessible.");
    const auto local = LocalAppData();
    if (local.empty()) snapshot.notes.push_back(L"Docker known paths unavailable: LOCALAPPDATA is unknown.");
    else {
        AddDisk(snapshot, disks, local + L"\\Docker\\wsl\\disk\\docker_data.vhdx", L"Docker data (modern)", L"Docker");
        AddDisk(snapshot, disks, local + L"\\Docker\\wsl\\data\\ext4.vhdx", L"Docker data (legacy)", L"Docker");
        AddDisk(snapshot, disks, local + L"\\Docker\\wsl\\data\\docker_data.vhdx", L"Docker data (alternate)", L"Docker");
    }
    snapshot.notes.push_back(L"Metadata only; no WSL or Docker process was started.");
    snapshot.notes.push_back(L"VHDX logical file length is not physical allocation, guest usage or reclaimable space; custom Docker locations may be absent.");
    snapshot.observed = Now();
    return snapshot;
}
} // namespace wc
