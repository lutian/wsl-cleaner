// Isolated native API compatibility check. Never attaches a disk or accesses WSL/Docker.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <virtdisk.h>
#include <objbase.h>
#include <iostream>
#include <string>

struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle() { if(value!=INVALID_HANDLE_VALUE && value) CloseHandle(value); }
    void Close() { if(value!=INVALID_HANDLE_VALUE && value) CloseHandle(value); value=INVALID_HANDLE_VALUE; }
};
int main() {
    wchar_t temp[32768]{}, unique[64]{};
    GUID guid{};
    const auto length=GetTempPathW(32768,temp);
    if(!length || length>=32768 || FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid,unique,64)) {
        std::cerr<<"FAIL: cannot allocate isolated temp identity\n"; return 1;
    }
    const std::wstring root=std::wstring(temp)+L"wsl-cleaner-vhd-check-"+unique;
    if(!CreateDirectoryW(root.c_str(),nullptr)) {
        std::cerr<<"FAIL: refusing existing/unavailable scratch directory\n"; return 1;
    }
    const std::wstring path=root+L"\\native-\u00e7.vhdx";
    auto fail=[&](const char* operation,DWORD status) {
        std::cerr<<"FAIL: "<<operation<<" DWORD="<<status<<"; isolated scratch retained\n";
        return 1;
    };
    VIRTUAL_STORAGE_TYPE storage{};
    storage.DeviceId=VIRTUAL_STORAGE_TYPE_DEVICE_VHDX;
    storage.VendorId=VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT;
    CREATE_VIRTUAL_DISK_PARAMETERS create{};
    create.Version=CREATE_VIRTUAL_DISK_VERSION_2;
    create.Version2.MaximumSize=64ULL*1024*1024;
    create.Version2.SectorSizeInBytes=512;
    create.Version2.PhysicalSectorSizeInBytes=512;
    Handle disk;
    DWORD status=CreateVirtualDisk(&storage,path.c_str(),VIRTUAL_DISK_ACCESS_NONE,nullptr,
        CREATE_VIRTUAL_DISK_FLAG_NONE,0,&create,nullptr,&disk.value);
    if(status!=ERROR_SUCCESS) return fail("CreateVirtualDisk on new scratch",status);
    disk.Close();
    OPEN_VIRTUAL_DISK_PARAMETERS open{};
    open.Version=OPEN_VIRTUAL_DISK_VERSION_2;
    open.Version2.GetInfoOnly=FALSE;
    open.Version2.ReadOnly=FALSE;
    status=OpenVirtualDisk(&storage,path.c_str(),VIRTUAL_DISK_ACCESS_NONE,
        OPEN_VIRTUAL_DISK_FLAG_NONE,&open,&disk.value);
    if(status!=ERROR_SUCCESS) return fail("OpenVirtualDisk UTF-16 metadata handle",status);
    GET_VIRTUAL_DISK_INFO info{};
    info.Version=GET_VIRTUAL_DISK_INFO_IS_LOADED;
    ULONG bytes=sizeof(info);
    status=GetVirtualDiskInformation(disk.value,&bytes,&info,nullptr);
    if(status!=ERROR_SUCCESS || info.IsLoaded) return fail("detached-state precondition",status);
    COMPACT_VIRTUAL_DISK_PARAMETERS compact{};
    compact.Version=COMPACT_VIRTUAL_DISK_VERSION_1;
    status=CompactVirtualDisk(disk.value,COMPACT_VIRTUAL_DISK_FLAG_NONE,&compact,nullptr);
    if(status!=ERROR_SUCCESS) return fail("CompactVirtualDisk new empty dynamic VHDX",status);
    info={}; info.Version=GET_VIRTUAL_DISK_INFO_IS_LOADED; bytes=sizeof(info);
    status=GetVirtualDiskInformation(disk.value,&bytes,&info,nullptr);
    if(status!=ERROR_SUCCESS || info.IsLoaded) return fail("detached-state postcondition",status);
    disk.Close();
    if(!DeleteFileW(path.c_str()) || !RemoveDirectoryW(root.c_str())) return fail("remove only owned scratch",GetLastError());
    std::cout<<"PASS native UTF-16 create/open/detached/compact/dispose; no disk attached, no WSL/Docker access\n";
    return 0;
}
