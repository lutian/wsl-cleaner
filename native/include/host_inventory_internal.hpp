#pragma once
#include "wsl_cleaner.hpp"
#include <vector>

namespace wc::internal {
// Callbacks keep Windows metadata queries isolated from deterministic checks.
template<class MountPaths, class Space>
void CollectMountedHost(Snapshot& snapshot, const std::wstring& volume,
                        MountPaths mount_paths, Space space) {
    std::vector<wchar_t> paths(32768, L'\0');
    if (!mount_paths(paths.data(), paths.size())) {
        snapshot.notes.push_back(L"Host mount paths unavailable: " + volume);
        return;
    }
    if (!paths[0]) return;
    HostDisk disk;
    disk.root = paths.data();
    if (!space(disk.total, disk.free)) {
        disk.total = disk.free = 0;
        snapshot.notes.push_back(L"Host capacity/free space unknown: " + disk.root);
    }
    snapshot.hosts.push_back(disk);
}
} // namespace wc::internal
