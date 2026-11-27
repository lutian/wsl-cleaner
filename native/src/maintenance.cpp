#include "maintenance_internal.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <cwctype>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <initguid.h>
#include <virtdisk.h>
#include <cstring>
#endif

namespace wc {
std::wstring QuoteArgument(const std::wstring& value) {
    if (value.find(L'\0') != std::wstring::npos) throw std::invalid_argument("NUL in process argument");
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch; slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'\"';
    return result;
}
namespace {
using namespace maintenance;
#ifdef _WIN32
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
bool Fail(std::wstring& error, const wchar_t* message) { error = message; return false; }
struct Pins {
    std::vector<std::unique_ptr<Handle>> handles;
    std::wstring path;
    // Pin each existing component against rename, replacement and write opens.
    // No lexical '..', alternate streams, device prefixes, UNC or reparse points.
    bool Open(const std::wstring& input, bool directory, std::wstring& error,
        DWORD leaf_share = FILE_SHARE_READ, DWORD leaf_access = FILE_READ_ATTRIBUTES | GENERIC_READ) {
        if (input.size() < 3 || input.size() > 30000 || input[1] != L':' || input[2] != L'\\' ||
            !((input[0] >= L'A' && input[0] <= L'Z') || (input[0] >= L'a' && input[0] <= L'z')) ||
            input.find_first_of(L"/\r\n") != std::wstring::npos || input.find(L'\0') != std::wstring::npos ||
            input.find(L':', 2) != std::wstring::npos || GetDriveTypeW(input.substr(0, 3).c_str()) != DRIVE_FIXED)
            return Fail(error, L"Only unambiguous local fixed-volume paths are supported.");
        path = input;
        if (path.size() > 3 && path.back() == L'\\') path.pop_back();
        std::size_t end = 3;
        for (;;) {
            const auto part = path.substr(0, end);
            if (end > 3) {
                const auto slash = part.find_last_of(L'\\');
                const auto name = part.substr(slash + 1);
                if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ')
                    return Fail(error, L"Ambiguous path component rejected.");
            }
            const bool leaf = end == path.size();
            auto handle = std::make_unique<Handle>(CreateFileW(part.c_str(), leaf ? leaf_access : FILE_READ_ATTRIBUTES,
                leaf ? leaf_share : FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            BY_HANDLE_FILE_INFORMATION info{};
            if (handle->value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle->value, &info) ||
                (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                ((end < path.size() || directory) && !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ||
                (end == path.size() && !directory && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)))
                return Fail(error, L"Cannot pin path components without reparse traversal or concurrent writers.");
            handles.push_back(std::move(handle));
            if (end == path.size()) return true;
            const auto next = path.find(L'\\', end == 3 ? end : end + 1);
            end = next == std::wstring::npos ? path.size() : next;
        }
    }
};
bool HexId(const std::wstring& input, bool image = false) {
    const auto start = image && input.compare(0, 7, L"sha256:") == 0 ? 7U : 0U;
    return input.size() == start + 64 && std::all_of(input.begin() + start, input.end(), [](wchar_t c) {
        return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
    });
}
std::vector<std::wstring> Lines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto end = text.find(L'\n', begin);
        auto line = text.substr(begin, end == std::wstring::npos ? end : end - begin);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) lines.push_back(std::move(line));
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    return lines;
}
class MutexGuard final : public OfflineGuard {
    Handle mutex_;
    bool& active_;
    DWORD thread_ = GetCurrentThreadId();
public:
    MutexGuard(HANDLE mutex, bool& active) : mutex_(mutex), active_(active) { active_ = true; }
    ~MutexGuard() override { active_ = false; ReleaseMutex(mutex_.value); }
    bool Verify(std::wstring& error) override {
        return thread_ == GetCurrentThreadId() || Fail(error, L"Global maintenance mutex ownership changed.");
    }
};
bool EnvironmentSafe(std::wstring& error) {
    LPWCH block = GetEnvironmentStringsW();
    if (!block) return Fail(error, L"Cannot validate Docker environment.");
    bool safe = true;
    for (const wchar_t* entry = block; *entry; entry += wcslen(entry) + 1) {
        if (_wcsnicmp(entry, L"DOCKER_", 7) == 0 || _wcsnicmp(entry, L"BUILDX_", 7) == 0) { safe = false; break; }
    }
    FreeEnvironmentStringsW(block);
    return safe || Fail(error, L"Docker/Buildx environment overrides are not supported; remove them before maintenance.");
}
std::wstring DockerPath(const wchar_t* relative, std::wstring& error) {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &folder))) {
        error = L"Cannot locate the system Program Files directory."; return {};
    }
    const std::wstring executable = std::wstring(folder) + L"\\Docker\\Docker\\resources\\" + relative;
    CoTaskMemFree(folder);
    return executable;
}
bool DockerSignature(const Pins& pin, std::wstring& error) {
    WINTRUST_FILE_INFO file{}; file.cbStruct = sizeof(file); file.pcwszFilePath = pin.path.c_str();
    file.hFile = pin.handles.back()->value;
    WINTRUST_DATA trust{}; trust.cbStruct = sizeof(trust); trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE; trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file; trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const auto status = WinVerifyTrust(nullptr, &policy, &trust);
    bool docker_signer = false;
    if (status == ERROR_SUCCESS) {
        auto* provider = WTHelperProvDataFromStateData(trust.hWVTStateData);
        auto* signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        auto* cert = signer ? WTHelperGetProvCertFromChain(signer, 0) : nullptr;
        if (cert && cert->pCert) {
            wchar_t name[256]{};
            if (CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256) > 1)
                docker_signer = wcscmp(name, L"Docker Inc") == 0 || wcscmp(name, L"Docker Inc.") == 0;
        }
    }
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &trust);
    return (status == ERROR_SUCCESS && docker_signer) || Fail(error, L"Docker executable does not have a trusted Docker publisher signature.");
}
bool TrustedDocker(Pins& pin, std::wstring& error) {
    const auto path = DockerPath(L"bin\\docker.exe", error);
    return !path.empty() && pin.Open(path, false, error) && DockerSignature(pin, error);
}
bool LocalServer(const std::wstring& endpoint, Pins& backend, std::wstring& error, DWORD* server_pid = nullptr) {
    const auto pipe = endpoint == L"npipe:////./pipe/docker_engine" ? L"\\\\.\\pipe\\docker_engine" :
        endpoint == L"npipe:////./pipe/dockerDesktopLinuxEngine" ? L"\\\\.\\pipe\\dockerDesktopLinuxEngine" : L"";
    if (!*pipe) return Fail(error, L"Unsupported Docker endpoint.");
    Handle connection(CreateFileW(pipe, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
    ULONG pid = 0;
    if (connection.value == INVALID_HANDLE_VALUE || !GetNamedPipeServerProcessId(connection.value, &pid) || !pid)
        return Fail(error, L"Cannot authenticate the local Docker named-pipe server.");
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    wchar_t path[32768]{}; DWORD length = 32768;
    const auto expected = DockerPath(L"com.docker.backend.exe", error);
    if (!process.value || expected.empty() || !QueryFullProcessImageNameW(process.value, 0, path, &length) ||
        _wcsicmp(path, expected.c_str()) != 0 || !backend.Open(expected, false, error) || !DockerSignature(backend, error))
        return Fail(error, L"Docker named pipe is not served by the trusted installed Docker Desktop backend.");
    if (server_pid) *server_pid = pid;
    return true;
}
// No shell. Explicit executable, pinned installation, restricted inherited handles,
// bounded output/deadline, suspended creation then kill-on-close job assignment.
bool Process(const Pins& exe, const std::vector<std::wstring>& args, ULONGLONG deadline,
    std::wstring& output, std::wstring& error, const std::string& stdin_bytes = {}, unsigned timeout_ms = 15000) {
    if (stdin_bytes.size() > 4096) return Fail(error, L"Allowlisted tool input exceeds 4096 bytes.");
    std::wstring command = QuoteArgument(exe.path);
    for (const auto& arg : args) {
        if (arg.size() > 8192 || arg.find(L'\0') != std::wstring::npos) return Fail(error, L"Invalid process argument.");
        command += L" " + QuoteArgument(arg);
    }
    if (command.size() > 30000 || GetTickCount64() >= deadline) return Fail(error, L"Maintenance process deadline exceeded.");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read_raw = nullptr, write_raw = nullptr;
    if (!CreatePipe(&read_raw, &write_raw, &security, 0)) return Fail(error, L"Cannot create process output pipe.");
    Handle read(read_raw), write(write_raw);
    Handle input, input_writer;
    if (stdin_bytes.empty()) input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    else if (!CreatePipe(&input.value, &input_writer.value, &security, 8192) ||
        !SetHandleInformation(input_writer.value, HANDLE_FLAG_INHERIT, 0)) return Fail(error, L"Cannot create bounded tool input.");
    if (input.value == INVALID_HANDLE_VALUE || !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0))
        return Fail(error, L"Cannot restrict process handles.");
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) return Fail(error, L"Cannot initialize process handle list.");
    struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST value; ~Attributes() { DeleteProcThreadAttributeList(value); } } cleanup{attributes};
    HANDLE inherited[] = {write.value, input.value};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
        return Fail(error, L"Cannot restrict inherited process handles.");
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return Fail(error, L"Cannot establish bounded process job.");
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = write.value;
    startup.StartupInfo.hStdInput = input.value;
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.path.c_str(), &command[0], nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
        exe.path.substr(0, exe.path.find_last_of(L'\\')).c_str(), &startup.StartupInfo, &info))
        return Fail(error, L"Cannot start the trusted Windows/official tool process.");
    Handle process(info.hProcess), thread(info.hThread);
    if (!AssignProcessToJobObject(job.value, process.value) || ResumeThread(thread.value) == static_cast<DWORD>(-1)) {
        TerminateProcess(process.value, 1); WaitForSingleObject(process.value, 5000);
        return Fail(error, L"Cannot constrain child process; it was not allowed to execute.");
    }
    struct Reap {
        HANDLE job, process;
        ~Reap() { TerminateJobObject(job, 1); WaitForSingleObject(process, 5000); }
    } reap{job.value, process.value};
    CloseHandle(write.value); write.value = INVALID_HANDLE_VALUE;
    if (!stdin_bytes.empty()) {
        DWORD sent = 0;
        if (!WriteFile(input_writer.value, stdin_bytes.data(), static_cast<DWORD>(stdin_bytes.size()), &sent, nullptr) || sent != stdin_bytes.size())
            return Fail(error, L"Cannot deliver bounded allowlisted tool input.");
        CloseHandle(input_writer.value); input_writer.value = INVALID_HANDLE_VALUE;
    }
    std::string captured;
    const auto end = std::min(deadline, GetTickCount64() + timeout_ms);
    bool broken = false;
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr)) {
            if (GetLastError() != ERROR_BROKEN_PIPE) return Fail(error, L"Cannot read process output.");
            broken = true;
        }
        if (available) {
            char buffer[4096]; DWORD used = 0;
            if (!ReadFile(read.value, buffer, std::min<DWORD>(available, sizeof(buffer)), &used, nullptr))
                return Fail(error, L"Cannot read bounded process output.");
            captured.append(buffer, used);
            if (captured.size() > 1024 * 1024) return Fail(error, L"Tool output exceeded 1 MiB limit; child job terminated.");
        } else if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) {
            if (broken || available == 0) break;
        }
        if (GetTickCount64() >= end) return Fail(error, L"Tool command timed out; child job terminated. Mutation outcome may be unknown.");
        Sleep(5);
    }
    DWORD code = 1;
    if (!GetExitCodeProcess(process.value, &code) || code != 0)
        return Fail(error, L"Official tool returned failure. Mutation outcome may be partial; no automatic retry.");
    if (captured.find('\0') != std::string::npos) return Fail(error, L"Unexpected NUL in tool output.");
    const int count = captured.empty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, captured.data(), static_cast<int>(captured.size()), nullptr, 0);
    if (!captured.empty() && count <= 0) return Fail(error, L"Tool output was not valid UTF-8; operation outcome may be partial.");
    output.assign(static_cast<std::size_t>(count), L'\0');
    if (count && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, captured.data(), static_cast<int>(captured.size()), &output[0], count))
        return Fail(error, L"Cannot decode tool output.");
    return true;
}
std::wstring FileIdentity(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return {};
    return std::to_wstring(info.dwVolumeSerialNumber) + L":" + std::to_wstring(info.nFileIndexHigh) + L":" + std::to_wstring(info.nFileIndexLow);
}
std::wstring Volume(const std::wstring& path) {
    wchar_t root[32768]{}, volume[128]{};
    if (!GetVolumePathNameW(path.c_str(), root, 32768) || GetDriveTypeW(root) != DRIVE_FIXED ||
        !GetVolumeNameForVolumeMountPointW(root, volume, 128)) return {};
    return volume;
}
bool Measure(Pins& pins, OfflineTarget& target, std::wstring& error) {
    LARGE_INTEGER bytes{};
    target.identity = FileIdentity(pins.handles.back()->value); target.volume = Volume(pins.path);
    if (target.identity.empty() || target.volume.empty() || !GetFileSizeEx(pins.handles.back()->value, &bytes) || bytes.QuadPart <= 0)
        return Fail(error, L"Cannot obtain fixed-volume file identity and size.");
    target.bytes = static_cast<std::uint64_t>(bytes.QuadPart); target.fixed_volume = true; target.reparse = false;
    return true;
}
bool OpenVhd(const std::wstring& path, Handle& disk, std::wstring& error, bool for_compact = false) {
    VIRTUAL_STORAGE_TYPE storage{}; storage.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_VHDX;
    storage.VendorId = VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT;
    OPEN_VIRTUAL_DISK_PARAMETERS parameters{}; parameters.Version = OPEN_VIRTUAL_DISK_VERSION_2;
    parameters.Version2.GetInfoOnly = for_compact ? FALSE : TRUE;
    parameters.Version2.ReadOnly = for_compact ? FALSE : TRUE;
    const DWORD status = OpenVirtualDisk(&storage, path.c_str(), VIRTUAL_DISK_ACCESS_NONE,
        OPEN_VIRTUAL_DISK_FLAG_NONE, &parameters, &disk.value);
    if (status != ERROR_SUCCESS) {
        error = L"OpenVirtualDisk failed (DWORD " + std::to_wstring(status) + L").";
        return false;
    }
    GET_VIRTUAL_DISK_INFO info{}; info.Version = GET_VIRTUAL_DISK_INFO_PROVIDER_SUBTYPE; ULONG size = sizeof(info);
    if (GetVirtualDiskInformation(disk.value, &size, &info, nullptr) != ERROR_SUCCESS || info.ProviderSubtype != 3)
        return Fail(error, L"Only standalone dynamically expanding VHDX disks are supported; fixed/differencing/unknown disks refused.");
    return true;
}
bool DetachedHandle(HANDLE disk, std::wstring& error) {
    GET_VIRTUAL_DISK_INFO info{}; info.Version = GET_VIRTUAL_DISK_INFO_IS_LOADED; ULONG size = sizeof(info);
    if (GetVirtualDiskInformation(disk, &size, &info, nullptr) != ERROR_SUCCESS || info.IsLoaded)
        return Fail(error, L"VHDX is attached or attachment state is unknown; refusing offline maintenance.");
    return true;
}
bool Detached(const std::wstring& path, std::wstring& error) {
    Handle disk;
    return OpenVhd(path, disk, error) && DetachedHandle(disk.value, error);
}
bool SameTarget(const OfflineTarget& a, const OfflineTarget& b) {
    return a.identity == b.identity && a.volume == b.volume && a.bytes == b.bytes;
}
class SourceGuard final : public OfflineGuard {
public:
    Pins pins;
    OfflineTarget expected;
    bool Acquire(const std::wstring& path, const OfflineTarget& identity, std::wstring& error) {
        expected = identity;
        if (!Detached(path, error) || !pins.Open(path, false, error, 0)) return false;
        return Verify(error);
    }
    bool Verify(std::wstring& error) override {
        OfflineTarget current;
        if (pins.handles.empty() || pins.handles.back()->value == INVALID_HANDLE_VALUE || !Measure(pins, current, error) || !SameTarget(current, expected))
            return Fail(error, L"Exclusive source identity/size guard failed.");
        return true;
    }
    void ReleaseFile() { CloseHandle(pins.handles.back()->value); pins.handles.back()->value = INVALID_HANDLE_VALUE; }
};
struct Sha256 {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    ~Sha256() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    bool Start() {
        DWORD length = 0, used = 0;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
            BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &used, 0) != 0 || !length || length > 1048576) return false;
        object.resize(length);
        return BCryptCreateHash(algorithm, &hash, object.data(), length, nullptr, 0, 0) == 0;
    }
    bool Add(unsigned char* data, ULONG bytes) { return BCryptHashData(hash, data, bytes, 0) == 0; }
    std::wstring Finish() {
        unsigned char digest[32];
        if (BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0) return {};
        std::wstring result;
        for (const auto byte : digest) { result += L"0123456789abcdef"[byte >> 4]; result += L"0123456789abcdef"[byte & 15]; }
        return result;
    }
};
bool WriteBytes(HANDLE file, const void* data, DWORD size) {
    DWORD written = 0; return WriteFile(file, data, size, &written, nullptr) && written == size;
}
bool Utf8(const std::wstring& text, std::string& bytes) {
    if (text.size() > 65536) return false;
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0 || length > 262144) return false;
    bytes.assign(static_cast<std::size_t>(length), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), &bytes[0], length, nullptr, nullptr) == length;
}
bool WriteManifest(HANDLE file, const std::wstring& text) {
    std::string bytes;
    return Utf8(text, bytes) && WriteBytes(file, bytes.data(), static_cast<DWORD>(bytes.size())) && FlushFileBuffers(file);
}
bool ApplicationDirectory(Pins& pins, std::wstring& error) {
    const auto directory = DataDirectory();
    PWSTR profile = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &profile)))
        return Fail(error, L"Cannot authenticate the application-data location.");
    const auto expected = std::wstring(profile) + L"\\WslCleaner"; CoTaskMemFree(profile);
    if (_wcsicmp(directory.c_str(), expected.c_str()) != 0) return Fail(error, L"Application-data environment redirection is not permitted.");
    const auto slash = directory.find_last_of(L'\\');
    Pins parent;
    if (slash == std::wstring::npos || !parent.Open(directory.substr(0, slash), true, error, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_READ_ATTRIBUTES)) return false;
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return Fail(error, L"Cannot create application-data directory.");
    return pins.Open(directory, true, error, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_READ_ATTRIBUTES);
}
bool CliConfiguration(Pins& directory, Pins& file, std::wstring& error) {
    Pins application;
    if (!ApplicationDirectory(application, error)) return false;
    const auto path = application.path + L"\\maintenance-cli";
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return Fail(error, L"Cannot stage restricted Docker CLI configuration.");
    if (!directory.Open(path, true, error, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_READ_ATTRIBUTES)) return false;
    const auto plugins = DockerPath(L"cli-plugins", error);
    if (plugins.empty()) return false;
    std::wstring quoted;
    for (const auto c : plugins) { if (c == L'\\' || c == L'\"') quoted += L'\\'; quoted += c; }
    const auto text = L"{\"cliPluginsExtraDirs\":[\"" + quoted + L"\"]}\n";
    const auto config = path + L"\\config.json";
    Handle fresh(CreateFileW(config.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr));
    if (fresh.value != INVALID_HANDLE_VALUE) {
        if (!WriteManifest(fresh.value, text)) return Fail(error, L"Cannot flush restricted CLI configuration.");
        CloseHandle(fresh.value); fresh.value = INVALID_HANDLE_VALUE;
    } else if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return Fail(error, L"Cannot create restricted CLI configuration.");
    if (!file.Open(config, false, error)) return false;
    std::string expected;
    if (!Utf8(text, expected)) return Fail(error, L"Invalid trusted plugin path encoding.");
    std::vector<char> actual(expected.size() + 1); DWORD read = 0;
    if (!ReadFile(file.handles.back()->value, actual.data(), static_cast<DWORD>(actual.size()), &read, nullptr) ||
        read != expected.size() || std::string(actual.data(), read) != expected) return Fail(error, L"Restricted CLI configuration changed or is incomplete; refusing command.");
    return true;
}
bool SystemProcess(const wchar_t* name, const std::vector<std::wstring>& arguments, unsigned timeout,
    std::wstring& output, std::wstring& error, const std::string& input = {}) {
    wchar_t system[32768]{}; const auto length = GetSystemDirectoryW(system, 32768);
    if (!length || length >= 32768) return Fail(error, L"Cannot resolve a trusted Windows system tool.");
    Pins executable;
    if (!executable.Open(std::wstring(system) + L"\\" + name, false, error)) return false;
    return Process(executable, arguments, GetTickCount64() + timeout, output, error, input, timeout);
}
class WindowsAdapter final : public Adapter {
    std::wstring endpoint_, context_, engine_;
    DWORD backend_pid_ = 0;
    ULONGLONG deadline_ = 0;
    bool active_ = false;
    std::unique_ptr<Pins> journal_directory_;
    std::unique_ptr<Handle> journal_;
    std::wstring journal_token_;
    std::unique_ptr<Pins> stop_plugin_;
    std::map<std::wstring, std::wstring> destination_ids_;
    std::uint64_t reserve_ = 0;
    bool Run(const std::vector<std::wstring>& args, std::wstring& out, std::wstring& error, bool connected = true, unsigned timeout = 15000, bool isolated = true) {
        if (!EnvironmentSafe(error)) return false;
        if (!deadline_) deadline_ = GetTickCount64() + 90000;
        Pins exe;
        if (!TrustedDocker(exe, error)) return false;
        std::vector<std::wstring> argv;
        Pins configuration, configuration_file;
        if (isolated) {
            if (!CliConfiguration(configuration, configuration_file, error)) return false;
            argv = {L"--config", configuration.path};
        }
        Pins backend;
        if (connected) {
            if (endpoint_.empty()) return Fail(error, L"Docker endpoint has not been validated.");
            DWORD pid = 0;
            if (!LocalServer(endpoint_, backend, error, &pid) || (backend_pid_ && backend_pid_ != pid)) return Fail(error, L"Local Docker backend changed; prepare using a new service.");
            backend_pid_ = pid;
            argv.push_back(L"--host"); argv.push_back(endpoint_);
        }
        argv.insert(argv.end(), args.begin(), args.end());
        return Process(exe, argv, deadline_, out, error, {}, timeout);
    }
public:
    std::uint64_t Clock() override {
        FILETIME file{}; GetSystemTimeAsFileTime(&file);
        ULARGE_INTEGER ticks{}; ticks.LowPart = file.dwLowDateTime; ticks.HighPart = file.dwHighDateTime;
        constexpr std::uint64_t offset = 116444736000000000ULL;
        return ticks.QuadPart < offset ? 0 : (ticks.QuadPart - offset) / 10000000;
    }
    std::wstring FreshToken() override {
        unsigned char bytes[32];
        if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return {};
        std::wstring token;
        for (const auto byte : bytes) { token += L"0123456789abcdef"[byte >> 4]; token += L"0123456789abcdef"[byte & 15]; }
        return token;
    }
    std::unique_ptr<OfflineGuard> LockMaintenance(std::wstring& error) override {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;0x001f0001;;;SY)(A;;0x001f0001;;;BA)(A;;0x00120001;;;AU)", SDDL_REVISION_1, &descriptor, nullptr)) {
            error = L"Cannot establish global maintenance mutex security."; return nullptr;
        }
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        HANDLE mutex = CreateMutexExW(&security, L"Global\\WslCleaner.NativeMaintenance.v1", 0,
            SYNCHRONIZE | MUTEX_MODIFY_STATE | READ_CONTROL);
        // READ_CONTROL must be allowed to authenticated users as well, so an
        // unprivileged instance can validate rather than trust a precreated object.
        if (!mutex) { LocalFree(descriptor); error = L"Cannot securely open global maintenance mutex."; return nullptr; }
        DWORD required = 0;
        GetKernelObjectSecurity(mutex, DACL_SECURITY_INFORMATION, nullptr, 0, &required);
        if (required > 65536) { LocalFree(descriptor); CloseHandle(mutex); error = L"Global mutex security descriptor exceeds the safety limit."; return nullptr; }
        std::vector<unsigned char> actual(required);
        PACL expected_acl = nullptr, actual_acl = nullptr; BOOL present = FALSE, defaulted = FALSE;
        bool valid = required && GetKernelObjectSecurity(mutex, DACL_SECURITY_INFORMATION, actual.data(), required, &required) &&
            GetSecurityDescriptorDacl(descriptor, &present, &expected_acl, &defaulted) && present && expected_acl &&
            GetSecurityDescriptorDacl(actual.data(), &present, &actual_acl, &defaulted) && present && actual_acl &&
            expected_acl->AclSize == actual_acl->AclSize && std::memcmp(expected_acl, actual_acl, expected_acl->AclSize) == 0;
        LocalFree(descriptor);
        const DWORD waited = valid ? WaitForSingleObject(mutex, 0) : WAIT_FAILED;
        if (waited != WAIT_OBJECT_0) {
            if (waited == WAIT_ABANDONED) ReleaseMutex(mutex);
            CloseHandle(mutex); error = L"Global mutex unavailable, insecure or abandoned; no automatic maintenance recovery."; return nullptr;
        }
        deadline_ = GetTickCount64() + 6ULL * 60 * 60 * 1000;
        journal_.reset(); journal_directory_.reset(); journal_token_.clear();
        return std::make_unique<MutexGuard>(mutex, active_);
    }
    bool Docker(DockerState& state, std::wstring& error) override {
        if (!active_) deadline_ = GetTickCount64() + 90000;
        state = {};
        if (!EnvironmentSafe(error)) { state.remote_environment = true; return false; }
        std::wstring output;
        if (!Run({L"context", L"show"}, output, error, false, 15000, false)) return false;
        auto names = Lines(output);
        if (names.size() != 1 || names[0].size() > 128 || names[0].empty() || names[0][0] == L'-')
            return Fail(error, L"Cannot determine an unambiguous Docker context.");
        if (!Run({L"context", L"inspect", L"--format", L"{{.Endpoints.docker.Host}}", names[0]}, output, error, false, 15000, false)) return false;
        auto hosts = Lines(output);
        if (hosts.size() != 1 || (hosts[0] != L"npipe:////./pipe/docker_engine" && hosts[0] != L"npipe:////./pipe/dockerDesktopLinuxEngine"))
            return Fail(error, L"Only official local Docker named-pipe endpoints are supported.");
        if ((!context_.empty() && context_ != names[0]) || (!endpoint_.empty() && endpoint_ != hosts[0]))
            return Fail(error, L"Docker context or endpoint changed; create a new maintenance service.");
        context_ = names[0]; endpoint_ = hosts[0];
        // Unlike docker info, version does not enumerate/execute arbitrary CLI plugin metadata.
        if (!Run({L"version", L"--format", L"{{.Server.Os}}\n{{.Server.Platform.Name}}\n{{.Server.Version}}"}, output, error)) return false;
        const auto identity = Lines(output);
        if (identity.size() != 3 || identity[0] != L"linux" || identity[1].size() > 256 || identity[2].size() > 128 ||
            (!engine_.empty() && engine_ != identity[1] + L":" + identity[2])) return Fail(error, L"Docker server type/version changed or is unsupported.");
        engine_ = identity[1] + L":" + identity[2]; state.local_engine = state.local_context = true;
        if (!Run({L"container", L"ls", L"--quiet", L"--no-trunc"}, output, error)) return false;
        state.any_running_containers = !Lines(output).empty();
        for (const auto& id : Lines(output)) if (!HexId(id)) return Fail(error, L"Docker running-container IDs are malformed.");
        return true;
    }
    bool ScopedCleanup(ObjectKind kind, unsigned days, std::wstring& error) override {
        DockerState state;
        if (!active_ || !Docker(state, error) || !state.local_engine || !state.local_context || state.remote_environment)
            return Fail(error, L"Scoped cleanup requires the global lease and a freshly validated local engine.");
        std::wstring output;
        return Run(CleanupArguments(kind, days), output, error, true, 120000);
    }
    bool Wsl(const std::wstring& name, WslState& state, std::wstring& error) override {
        state = {};
        if (name.empty() || name.size() > 256 || name[0] == L'-' || name.find_first_of(L"\r\n") != std::wstring::npos || name.find(L'\0') != std::wstring::npos)
            return Fail(error, L"Invalid registered distribution name.");
        std::wstring lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        state.docker_owned = lower == L"docker-desktop" || lower.compare(0, 15, L"docker-desktop-") == 0;
        const auto snapshot = Collect();
        for (const auto& disk : snapshot.disks) if (disk.name == name && disk.kind == L"Docker") state.docker_owned = true;
        wchar_t system[32768]{}; const auto length = GetSystemDirectoryW(system, 32768);
        if (!length || length >= 32768) return Fail(error, L"Cannot locate Windows WSL registration API.");
        Pins library;
        if (!library.Open(std::wstring(system) + L"\\wslapi.dll", false, error)) return false;
        HMODULE module = LoadLibraryExW(library.path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return Fail(error, L"Windows WSL registration API is unavailable.");
        using Registered = BOOL (WINAPI*)(PCWSTR);
        const auto entry = GetProcAddress(module, "WslIsDistributionRegistered");
        Registered registered = nullptr;
        static_assert(sizeof(registered) == sizeof(entry), "Windows function pointer size");
        std::memcpy(&registered, &entry, sizeof(registered));
        state.registered = registered && registered(name.c_str()); FreeLibrary(module);
        if (!state.registered || state.docker_owned) return Fail(error, L"Distribution is unregistered, Docker-owned or registration could not be verified.");
        return true; // Running state is deliberately not inferred. CLEAN may start this named distro.
    }
    bool AptClean(const std::wstring& name, std::wstring& error) override {
        WslState state;
        if (!active_ || !Wsl(name, state, error)) return false;
        std::wstring output;
        return SystemProcess(L"wsl.exe", AptArguments(name), 120000, output, error);
    }
    bool Administrator() override {
        SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY; PSID admins = nullptr; BOOL member = FALSE;
        if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins)) return false;
        const BOOL checked = CheckTokenMembership(nullptr, admins, &member); FreeSid(admins);
        return checked && member;
    }
    bool Target(const std::wstring& path, OfflineTarget& target, std::wstring& error) override {
        bool known = false;
        for (const auto& disk : Collect().disks) {
            if (disk.measured && (disk.kind == L"WSL" || disk.kind == L"Docker") && _wcsicmp(disk.path.c_str(), path.c_str()) == 0) known = true;
        }
        if (!known || path.size() < 5 || _wcsicmp(path.c_str() + path.size() - 5, L".vhdx") != 0)
            return Fail(error, L"Compaction target must be a measured known WSL/Docker VHDX from fresh inventory.");
        Pins pins;
        if (!pins.Open(path, false, error, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_READ_ATTRIBUTES) || !Measure(pins, target, error)) return false;
        Handle disk;
        return OpenVhd(path, disk, error);
    }
    bool BackupDestination(const std::wstring& directory, const OfflineTarget& source, std::uint64_t reserve, std::wstring& error) override {
        Pins pins;
        if (!pins.Open(directory, true, error, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_READ_ATTRIBUTES)) return false;
        const auto volume = Volume(directory); const auto id = FileIdentity(pins.handles.back()->value);
        const auto found = destination_ids_.find(directory);
        if (volume.empty() || id.empty() || volume == source.volume || (found != destination_ids_.end() && found->second != id))
            return Fail(error, L"Backup requires an unchanged directory on a different local fixed volume, without reparse traversal.");
        constexpr std::uint64_t manifest_space = 65536;
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        ULARGE_INTEGER free{}, total{}, all_free{};
        if (source.bytes > maximum - manifest_space || reserve > maximum - source.bytes - manifest_space ||
            !GetDiskFreeSpaceExW(directory.c_str(), &free, &total, &all_free) || free.QuadPart < source.bytes + reserve + manifest_space)
            return Fail(error, L"Backup volume lacks space for the entire source plus configured reserve and manifest.");
        destination_ids_[directory] = id; reserve_ = reserve; return true;
    }
    bool CanStopDocker(std::wstring& error) override {
        std::wstring output;
        const auto expected = DockerPath(L"cli-plugins\\docker-desktop.exe", error);
        if (expected.empty()) return false;
        // The isolated --config puts this trusted plugin directory first; never
        // dispatch a user-selected plugin or run docker info plugin enumeration.
        stop_plugin_ = std::make_unique<Pins>();
        if (!stop_plugin_->Open(expected, false, error) || !DockerSignature(*stop_plugin_, error)) return false;
        return Run({L"desktop", L"stop", L"--help"}, output, error, false);
    }
    bool StopDocker(std::wstring& error) override {
        DockerState state;
        if (!active_ || !Docker(state, error) || state.any_running_containers || !CanStopDocker(error))
            return Fail(error, L"Docker stop refused: running containers or unverified clean-stop capability.");
        std::wstring output;
        return Run({L"desktop", L"stop"}, output, error, false, 120000);
    }
    bool ShutdownWsl(std::wstring& error) override {
        if (!active_) return Fail(error, L"WSL shutdown requires the maintenance lease and explicit consent.");
        std::wstring output; return SystemProcess(L"wsl.exe", {L"--shutdown"}, 120000, output, error);
    }
    std::unique_ptr<OfflineGuard> LockOffline(const std::wstring& path, const OfflineTarget& target, std::wstring& error) override {
        auto guard = std::make_unique<SourceGuard>();
        if (!active_ || !guard->Acquire(path, target, error)) return nullptr;
        return guard;
    }
    bool Backup(const std::wstring& path, const std::wstring& directory, OfflineGuard& lease, std::wstring& error) override {
        auto* source = dynamic_cast<SourceGuard*>(&lease);
        if (!active_ || !source || source->pins.path != path || !source->Verify(error) || !HexId(journal_token_) ||
            !BackupDestination(directory, source->expected, reserve_, error)) return Fail(error, L"Backup source/destination guard failed.");
        Pins destination;
        if (!destination.Open(directory, true, error, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_READ_ATTRIBUTES) ||
            FileIdentity(destination.handles.back()->value) != destination_ids_[directory]) return Fail(error, L"Backup directory changed.");
        const auto filename = directory + L"\\backup-" + journal_token_ + L".vhdx";
        Handle backup(CreateFileW(filename.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        if (backup.value == INVALID_HANDLE_VALUE) return Fail(error, L"Cannot create a fresh backup; existing backups are never overwritten.");
        const auto failed = [&]() { error = L"Backup/verification failed; partial backup preserved at " + filename; return false; };
        Sha256 original;
        if (!original.Start()) return failed();
        LARGE_INTEGER zero{};
        if (!SetFilePointerEx(source->pins.handles.back()->value, zero, nullptr, FILE_BEGIN)) return failed();
        std::vector<unsigned char> buffer(1024 * 1024);
        std::uint64_t copied = 0;
        for (;;) {
            DWORD used = 0;
            if (GetTickCount64() >= deadline_ || !source->Verify(error) ||
                !ReadFile(source->pins.handles.back()->value, buffer.data(), static_cast<DWORD>(buffer.size()), &used, nullptr)) return failed();
            if (!used) break;
            if (!original.Add(buffer.data(), used) || !WriteBytes(backup.value, buffer.data(), used)) return failed();
            copied += used;
            if (copied > source->expected.bytes) return failed();
            if (copied % (64ULL * 1024 * 1024) < used || copied == source->expected.bytes) {
                ULARGE_INTEGER available{}, total{}, free{};
                const auto remaining = source->expected.bytes - copied;
                if (!GetDiskFreeSpaceExW(directory.c_str(), &available, &total, &free) || available.QuadPart < reserve_ + remaining + 65536)
                    return failed();
            }
        }
        const auto hash = original.Finish();
        if (copied != source->expected.bytes || hash.empty() || !FlushFileBuffers(backup.value) ||
            !SetFilePointerEx(backup.value, zero, nullptr, FILE_BEGIN)) return failed();
        Sha256 verified;
        if (!verified.Start()) return failed();
        std::uint64_t read = 0;
        for (;;) {
            DWORD used = 0;
            if (GetTickCount64() >= deadline_ || !ReadFile(backup.value, buffer.data(), static_cast<DWORD>(buffer.size()), &used, nullptr)) return failed();
            if (!used) break;
            if (!verified.Add(buffer.data(), used)) return failed();
            read += used;
        }
        if (read != copied || verified.Finish() != hash || !source->Verify(error)) return failed();
        ULARGE_INTEGER available{}, total{}, free{};
        if (!GetDiskFreeSpaceExW(directory.c_str(), &available, &total, &free) || available.QuadPart < reserve_ + 65536) return failed();
        Handle manifest(CreateFileW((directory + L"\\backup-" + journal_token_ + L".manifest").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr));
        if (manifest.value == INVALID_HANDLE_VALUE || !WriteManifest(manifest.value,
            L"version=1\nsource=" + path + L"\nidentity=" + source->expected.identity + L"\nsource_volume=" + source->expected.volume +
            L"\nbackup_volume=" + Volume(directory) + L"\nbackup=" + filename + L"\nbackup_identity=" + FileIdentity(backup.value) +
            L"\ntoken=" + journal_token_ + L"\ncreated=" + std::to_wstring(Clock()) + L"\nbytes=" + std::to_wstring(copied) + L"\nsha256=" + hash + L"\nverified=1\n")) return failed();
        return true;
    }
    bool Journal(const std::wstring& token, const std::wstring& step, std::wstring& error) override {
        if (!HexId(token) || step.size() > 65536 || step.find_first_of(L"\r\n") != std::wstring::npos || step.find(L'\0') != std::wstring::npos)
            return Fail(error, L"Invalid journal token/step.");
        bool first = false;
        if (!journal_) {
            journal_directory_ = std::make_unique<Pins>();
            if (!ApplicationDirectory(*journal_directory_, error)) return false;
            const auto path = journal_directory_->path + L"\\maintenance-" + token + L".journal";
            journal_ = std::make_unique<Handle>(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                nullptr, CREATE_NEW, FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (journal_->value == INVALID_HANDLE_VALUE) { journal_.reset(); return Fail(error, L"Cannot create a fresh durable maintenance journal; no destructive operation permitted."); }
            journal_token_ = token; first = true;
        }
        if (journal_token_ != token) return Fail(error, L"Journal issuer changed.");
        const auto line = (first ? std::wstring(L"transaction-start\r\n") : std::wstring()) + step + L"\r\n";
        if (!WriteManifest(journal_->value, line)) return Fail(error, L"Cannot durably flush maintenance journal.");
        return true;
    }
    bool Compact(const std::wstring& path, OfflineGuard& lease, std::wstring& error) override {
        auto* source = dynamic_cast<SourceGuard*>(&lease);
        if (!active_ || !source || source->pins.path != path || !source->Verify(error)) return Fail(error, L"Compaction source guard failed.");
        // Explicit handoff: no claim that a closed source handle stays exclusive.
        source->ReleaseFile();
        if (!Detached(path, error)) return false;
        Pins final;
        OfflineTarget current;
        if (!final.Open(path, false, error, 0) || !Measure(final, current, error) || !SameTarget(current, source->expected))
            return Fail(error, L"VHDX changed or became busy before compactor handoff.");
        CloseHandle(final.handles.back()->value); final.handles.back()->value = INVALID_HANDLE_VALUE;

        Handle disk;
        if (!OpenVhd(path, disk, error, true)) return false;
        // Keep file identity/path protection, not exclusive data access, while the
        // official provider owns the read/write metadata handle. No script/codec.
        Pins identity;
        if (!identity.Open(path, false, error, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_READ_ATTRIBUTES) ||
            !Measure(identity, current, error) || !SameTarget(current, source->expected))
            return Fail(error, L"VHDX identity changed during metadata-handle handoff.");
        if (!DetachedHandle(disk.value, error)) return false;

        COMPACT_VIRTUAL_DISK_PARAMETERS parameters{};
        parameters.Version = COMPACT_VIRTUAL_DISK_VERSION_1;
        // Synchronous and non-preemptible on the maintenance worker. The API
        // itself refuses read/write-attached disks. No attach/detach or retries.
        const DWORD status = CompactVirtualDisk(disk.value, COMPACT_VIRTUAL_DISK_FLAG_NONE, &parameters, nullptr);
        const auto result = CompactStatus(static_cast<std::uint32_t>(status));
        if (!result.success) { error = result.message; return false; }
        if (!DetachedHandle(disk.value, error)) return false;

        CloseHandle(disk.value); disk.value = INVALID_HANDLE_VALUE;
        CloseHandle(identity.handles.back()->value); identity.handles.back()->value = INVALID_HANDLE_VALUE;
        if (!Detached(path, error)) return false;
        Pins after;
        if (!after.Open(path, false, error, 0) || FileIdentity(after.handles.back()->value) != source->expected.identity ||
            Volume(path) != source->expected.volume)
            return Fail(error, L"Post-compaction identity/offline verification failed; backup retained.");
        return true;
    }
};
#else
// Portable builds expose quoting and policy tests, never a host maintenance path.
class UnsupportedAdapter final : public Adapter {
    static bool No(std::wstring& error) { error = L"Native maintenance requires Windows."; return false; }
public:
    std::uint64_t Clock() override { return 0; }
    std::wstring FreshToken() override { return {}; }
    std::unique_ptr<OfflineGuard> LockMaintenance(std::wstring& e) override { No(e); return nullptr; }
    bool Docker(DockerState&, std::wstring& e) override { return No(e); }
    bool ScopedCleanup(ObjectKind, unsigned, std::wstring& e) override { return No(e); }
    bool Wsl(const std::wstring&, WslState&, std::wstring& e) override { return No(e); }
    bool AptClean(const std::wstring&, std::wstring& e) override { return No(e); }
    bool Administrator() override { return false; }
    bool Target(const std::wstring&, OfflineTarget&, std::wstring& e) override { return No(e); }
    bool BackupDestination(const std::wstring&, const OfflineTarget&, std::uint64_t, std::wstring& e) override { return No(e); }
    bool CanStopDocker(std::wstring& e) override { return No(e); }
    bool StopDocker(std::wstring& e) override { return No(e); }
    bool ShutdownWsl(std::wstring& e) override { return No(e); }
    std::unique_ptr<OfflineGuard> LockOffline(const std::wstring&, const OfflineTarget&, std::wstring& e) override { No(e); return nullptr; }
    bool Backup(const std::wstring&, const std::wstring&, OfflineGuard&, std::wstring& e) override { return No(e); }
    bool Journal(const std::wstring&, const std::wstring&, std::wstring& e) override { return No(e); }
    bool Compact(const std::wstring&, OfflineGuard&, std::wstring& e) override { return No(e); }
};
#endif
} // namespace
struct MaintenanceService::Impl {
#ifdef _WIN32
    WindowsAdapter adapter;
#else
    UnsupportedAdapter adapter;
#endif
    maintenance::Engine engine{adapter};
};
MaintenanceService::MaintenanceService() : impl_(std::make_unique<Impl>()) {}
MaintenanceService::~MaintenanceService() = default;
bool MaintenanceService::Prepare(Action action, const std::wstring& target, const Config& config,
    const std::wstring& directory, Plan& plan, std::wstring& error) {
    try { return impl_->engine.Prepare(action, target, config, directory, plan, error); }
    catch (...) {
        plan = {}; error = L"Native preparation failed unexpectedly; no maintenance was authorized. Staged CLI metadata may remain.";
        return false;
    }
}
Result MaintenanceService::Execute(const Plan& plan, const Approval& approval, const Config& config) {
    try { return impl_->engine.Execute(plan, approval, config); }
    catch (...) {
        return {false, L"Native maintenance interrupted by an unexpected error; outcome may be partial. Journal and backup artifacts are preserved; no retry or restore attempted."};
    }
}
} // namespace wc
