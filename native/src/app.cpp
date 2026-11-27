#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include "wsl_cleaner.hpp"

namespace {
constexpr UINT Finished = WM_APP + 1, Tray = WM_APP + 2;
constexpr UINT_PTR PollTimer = 1;
enum { Report=100, Disks, Budget, Reserve, Warning, Critical, Poll, Cooldown, Age,
       Auto, Cache, Dangling, Disposable, Save, Docker, Apt, Compact, Consent, Shutdown, Execute, Exit };
struct Work {
    enum Kind { Inventory, Prepared, Executed } kind = Inventory;
    wc::Snapshot snapshot;
    std::vector<wc::Alert> alerts;
    wc::Plan plan;
    std::wstring error, message;
};
struct App {
    HWND window=nullptr;
    HFONT font=nullptr;
    HBRUSH black=nullptr;
    std::thread worker;
    std::mutex result_mutex;
    std::unique_ptr<Work> pending;
    wc::Config config;
    wc::Snapshot snapshot;
    wc::AlertEngine alerts;
    wc::MaintenanceService maintenance;
    wc::Plan plan;
    bool busy=false, closing=false, valid=false, has_plan=false, tray=false, smoke=false;
    std::wstring config_path, status;
    std::uint64_t auto_last=0;
    bool auto_attempted=false;
} app;
HWND Control(int id) { return GetDlgItem(app.window,id); }
std::wstring Text(int id) {
    int n=GetWindowTextLengthW(Control(id));
    std::wstring s(static_cast<size_t>(n)+1,L'\0');
    GetWindowTextW(Control(id),s.data(),n+1); s.resize(n); return s;
}
void Set(int id,const std::wstring& s) { SetWindowTextW(Control(id),s.c_str()); }
bool Checked(int id) { return SendMessageW(Control(id),BM_GETCHECK,0,0)==BST_CHECKED; }
void Check(int id,bool yes) { SendMessageW(Control(id),BM_SETCHECK,yes?BST_CHECKED:BST_UNCHECKED,0); }
void Show() { ShowWindow(app.window,SW_RESTORE); SetForegroundWindow(app.window); }
std::wstring Lines(const std::wstring& text) {
    std::wstring result;
    for(size_t i=0;i<text.size();++i) {
        if(text[i]==L'\n' && (!i || text[i-1]!=L'\r')) result+=L'\r';
        result+=text[i];
    }
    return result;
}
void Render() {
    Set(Report,Lines(L"VHDX file length only; physical allocation and guest usage UNKNOWN.\r\n"
        L"Budgets are advisory, NOT filesystem quotas.\r\n"+app.status+L"\r\n"+
        wc::FormatSnapshot(app.snapshot,app.config)));
}
void Error(const std::wstring& s) { app.status=L"ERROR: "+s; Render(); Show(); }
void EnableActions() {
    for(int id : {Save,Docker,Apt,Compact,Execute}) EnableWindow(Control(id),!app.busy && !app.closing);
    EnableWindow(Control(Execute),!app.busy && app.has_plan && !app.closing);
}
template<class F> void Start(F action) {
    if(app.busy || app.closing) return;
    if(app.worker.joinable()) app.worker.join();
    app.busy=true; EnableActions();
    try {
        HWND window=app.window;
        app.worker=std::thread([window,action,result=std::make_unique<Work>()]() mutable {
            try { action(*result); }
            catch(const std::exception&) { result->error=L"Background operation failed (C++ exception)."; }
            catch(...) { result->error=L"Background operation failed (unknown exception)."; }
            {
                std::lock_guard<std::mutex> lock(app.result_mutex);
                app.pending=std::move(result);
            }
            // Ownership stays in a guarded slot, not an untracked message pointer.
            // A live timer also drains the slot if notification posting fails.
            PostMessageW(window,Finished,0,0);
        });
    } catch(...) { app.busy=false; EnableActions(); Error(L"Cannot start background operation."); }
}
void Inventory() {
    const auto config=app.config;
    const bool automation=app.valid;
    Start([config,automation](Work& w) {
        w.snapshot=wc::Collect();
        w.alerts=app.alerts.Evaluate(w.snapshot,config,wc::Now());
        bool docker_alert=false;
        for(const auto& a:w.alerts) {
            if(a.id==L"docker") docker_alert=true;
            for(const auto& d:w.snapshot.disks)
                if((a.id==d.id || a.id==L"budget:"+d.id) &&
                   (d.kind==L"Docker" || d.kind==L"docker")) docker_alert=true;
        }
        const auto now=wc::Now();
        if(automation && config.auto_cleanup && docker_alert &&
           (config.clean_build_cache || config.clean_dangling_images || config.clean_disposable_containers) &&
           (!app.auto_attempted || (now>=app.auto_last && now-app.auto_last>=std::max(config.cooldown_seconds,60U)))) {
            // Record the attempt BEFORE Prepare/Execute, including failures and exceptions.
            app.auto_attempted=true; app.auto_last=now;
            wc::Plan plan; std::wstring error;
            if(app.maintenance.Prepare(wc::Action::DockerCleanup,L"docker",config,L"",plan,error)) {
                auto r=app.maintenance.Execute(plan,{plan.token,L"CLEAN",false},config);
                w.message=(r.success?L"Automatic Docker cleanup completed: ":L"Automatic Docker cleanup FAILED: ")+r.message;
            } else w.message=L"Automatic Docker cleanup failed: "+error;
        }
    });
}
bool Number(int id,std::uint64_t& value) {
    auto s=Text(id); if(s.empty()) return false;
    value=0;
    for(wchar_t c:s) {
        if(c<L'0'||c>L'9'||value>(UINT64_MAX-(c-L'0'))/10) return false;
        value=value*10+(c-L'0');
    }
    return true;
}
std::wstring SelectedDisk() {
    auto i=SendMessageW(Control(Disks),CB_GETCURSEL,0,0);
    return i>=0 && static_cast<size_t>(i)<app.snapshot.disks.size()?app.snapshot.disks[i].id:L"";
}
void DisplayBudget() {
    auto it=app.config.budgets.find(SelectedDisk());
    Set(Budget,it==app.config.budgets.end()?L"0":std::to_wstring(it->second));
    app.has_plan=false; EnableActions();
}
void SaveSettings() {
    wc::Config c=app.config; std::uint64_t n=0;
    for(auto field : {Reserve,Warning,Critical,Poll,Cooldown,Age,Budget}) {
        if(!Number(field,n)) { Error(L"Every numeric field must contain an unsigned decimal integer."); return; }
        if(field!=Reserve && field!=Budget && n>MAXDWORD) { Error(L"Numeric value too large."); return; }
        switch(field) {
        case Reserve:c.host_reserve=n;break;
        case Warning:c.warning_percent=static_cast<unsigned>(n);break;
        case Critical:c.critical_percent=static_cast<unsigned>(n);break;
        case Poll:c.poll_seconds=static_cast<unsigned>(n);break;
        case Cooldown:c.cooldown_seconds=static_cast<unsigned>(n);break;
        case Age:c.minimum_age_days=static_cast<unsigned>(n);break;
        case Budget:
            if(!SelectedDisk().empty()) {
                if(n) c.budgets[SelectedDisk()]=n;
                else c.budgets.erase(SelectedDisk());
            }
            break;
        }
    }
    c.auto_cleanup=Checked(Auto); c.clean_build_cache=Checked(Cache);
    c.clean_dangling_images=Checked(Dangling); c.clean_disposable_containers=Checked(Disposable);
    std::wstring error;
    if(!wc::ValidateConfig(c,error) || !wc::SaveConfig(app.config_path,c,error)) { Error(error); return; }
    app.config=c; app.valid=true; app.has_plan=false;
    app.status=L"Settings explicitly saved. Automation uses only opted-in Docker categories.";
    KillTimer(app.window,PollTimer);
    // Elapsed check uses 64-bit monotonic time; never truncate configured seconds.
    if(!SetTimer(app.window,PollTimer,1000,nullptr)) {
        Error(L"Settings saved, but polling timer could not start. Exit and restart the app.");
        EnableActions(); return;
    }
    Render(); EnableActions();
}
std::wstring Folder() {
    BROWSEINFOW bi{}; bi.hwndOwner=app.window; bi.lpszTitle=L"Choose an existing backup directory (never the source disk directory)";
    bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    auto pidl=SHBrowseForFolderW(&bi); if(!pidl) return L"";
    wchar_t path[MAX_PATH]{}; bool ok=SHGetPathFromIDListW(pidl,path)!=FALSE;
    CoTaskMemFree(pidl); return ok?path:L"";
}
bool IsAdmin() {
    SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY; PSID sid=nullptr; BOOL yes=FALSE;
    if(AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid)) {
        CheckTokenMembership(nullptr,sid,&yes); FreeSid(sid);
    }
    return yes!=FALSE;
}
void Prepare(wc::Action action) {
    if(!app.valid) { Error(L"Save valid settings explicitly before maintenance. Invalid stores are not overwritten automatically."); return; }
    std::wstring target=L"docker";
    if(action!=wc::Action::DockerCleanup) {
        const auto selected=SelectedDisk();
        const auto found=std::find_if(app.snapshot.disks.begin(),app.snapshot.disks.end(),
            [&](const wc::VirtualDisk& item) { return item.id==selected; });
        if(found==app.snapshot.disks.end()) { Error(L"Select a discovered disk first."); return; }
        const auto* disk=&*found;
        if(action==wc::Action::WslAptCache) {
            if(disk->kind!=L"WSL") { Error(L"Select a registered non-Docker WSL2 distribution for apt cleanup."); return; }
            target=disk->name;
        } else target=disk->path;
    }
    std::wstring backup;
    if(action==wc::Action::Compact) {
        if(!IsAdmin()) {
            Error(L"Compaction requires administrator privileges. Exit this resident app, then explicitly run it as administrator. No automatic elevation is performed."); return;
        }
        backup=Folder(); if(backup.empty()) return;
    }
    app.has_plan=false; Set(Consent,L""); Check(Shutdown,false);
    auto config=app.config;
    Start([action,target,backup,config](Work& w) {
        w.kind=Work::Prepared;
        if(!app.maintenance.Prepare(action,target,config,backup,w.plan,w.error) && w.error.empty()) w.error=L"Plan preparation failed.";
    });
}
void ExecutePlan() {
    if(!app.has_plan) return;
    const bool compact=app.plan.action==wc::Action::Compact;
    if(Text(Consent)!=(compact?L"COMPACT":L"CLEAN")) { Error(L"Type the exact approval: CLEAN or COMPACT as shown in the plan. Prepare a fresh plan to review again."); app.has_plan=false; EnableActions(); return; }
    if(compact && !Checked(Shutdown)) { Error(L"Compaction requires explicit AllowShutdown consent. Prepare a fresh plan to review again."); app.has_plan=false; EnableActions(); return; }
    const auto plan=app.plan; const auto config=app.config;
    const wc::Approval approval{plan.token,Text(Consent),Checked(Shutdown)};
    app.has_plan=false;
    Start([plan,config,approval](Work& w) {
        w.kind=Work::Executed;
        auto result=app.maintenance.Execute(plan,approval,config);
        if(result.success) w.message=result.message; else w.error=result.message;
    });
}
void Child(const wchar_t* cls,const wchar_t* text,int id,int x,int y,int width,int height,DWORD style=0) {
    auto h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style|(id?WS_TABSTOP:0),x,y,width,height,app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    if(!h) throw std::runtime_error("control creation");
    SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(app.font),TRUE);
}
void Controls() {
    app.font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,FIXED_PITCH,L"Consolas");
    app.black=CreateSolidBrush(RGB(0,0,0));
    Child(L"EDIT",L"",Report,10,10,960,265,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER);
    Child(L"COMBOBOX",L"",Disks,10,285,590,200,CBS_DROPDOWNLIST|WS_VSCROLL);
    Child(L"STATIC",L"Budget bytes (0 = unset)",0,610,285,240,22);
    Child(L"EDIT",L"0",Budget,810,285,160,24,WS_BORDER|ES_AUTOHSCROLL);
    const wchar_t* labels[]={L"Reserve bytes",L"Warn %",L"Critical %",L"Poll sec",L"Cooldown sec",L"Min age days"};
    const int ids[]={Reserve,Warning,Critical,Poll,Cooldown,Age};
    for(int i=0;i<6;++i) {
        int x=10+(i%3)*320,y=322+(i/3)*55;
        Child(L"STATIC",labels[i],0,x,y,150,22);
        Child(L"EDIT",L"",ids[i],x+150,y,155,24,WS_BORDER|ES_AUTOHSCROLL);
    }
    Child(L"BUTTON",L"Opt in: auto Docker cleanup on Docker alert",Auto,10,435,470,24,BS_AUTOCHECKBOX);
    Child(L"BUTTON",L"Build cache",Cache,490,435,180,24,BS_AUTOCHECKBOX);
    Child(L"BUTTON",L"Dangling images",Dangling,680,435,280,24,BS_AUTOCHECKBOX);
    Child(L"BUTTON",L"Labeled disposable containers only",Disposable,10,465,470,24,BS_AUTOCHECKBOX);
    Child(L"BUTTON",L"Save settings",Save,490,465,180,28);
    Child(L"BUTTON",L"Plan Docker cleanup",Docker,10,510,260,30);
    Child(L"BUTTON",L"Plan WSL apt cache",Apt,280,510,260,30);
    Child(L"BUTTON",L"Plan offline compact",Compact,550,510,260,30);
    Child(L"STATIC",L"Exact CLEAN / COMPACT:",0,10,555,240,24);
    Child(L"EDIT",L"",Consent,255,555,180,26,WS_BORDER);
    Child(L"BUTTON",L"AllowShutdown: ALL WSL + Docker workloads stop",Shutdown,445,555,525,26,BS_AUTOCHECKBOX);
    Child(L"BUTTON",L"Execute reviewed plan",Execute,10,600,300,32);
    Child(L"BUTTON",L"Exit (waits for maintenance)",Exit,640,600,330,32);
    Child(L"STATIC",L"Close hides to tray. Never auto compact/WSL cleanup. No volumes or ordinary stopped containers.",0,10,647,960,24);
    Set(Reserve,std::to_wstring(app.config.host_reserve)); Set(Warning,std::to_wstring(app.config.warning_percent));
    Set(Critical,std::to_wstring(app.config.critical_percent)); Set(Poll,std::to_wstring(app.config.poll_seconds));
    Set(Cooldown,std::to_wstring(app.config.cooldown_seconds)); Set(Age,std::to_wstring(app.config.minimum_age_days));
    Check(Auto,app.config.auto_cleanup); Check(Cache,app.config.clean_build_cache);
    Check(Dangling,app.config.clean_dangling_images); Check(Disposable,app.config.clean_disposable_containers);
    EnableActions(); Render();
}
void ExitApp() {
    app.closing=true;
    if(!app.busy) KillTimer(app.window,PollTimer);
    EnableActions();
    if(app.busy) { app.status=L"Exit requested: waiting for active operation safely; do not terminate during maintenance."; Render(); Show(); }
    else DestroyWindow(app.window);
}
LRESULT WindowProcImpl(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CLOSE: if(app.tray) ShowWindow(window,SW_HIDE); else Show(); return 0;
    case WM_QUERYENDSESSION:
        // Do not destroy live worker state during a normal interactive shutdown.
        return app.busy?FALSE:TRUE;
    case WM_TIMER: {
        bool completed=false;
        { std::lock_guard<std::mutex> lock(app.result_mutex); completed=app.pending!=nullptr; }
        if(completed) { SendMessageW(window,Finished,0,0); return 0; }
        static ULONGLONG last=GetTickCount64();
        auto now=GetTickCount64();
        if(!app.busy && !app.closing && now-last>=static_cast<ULONGLONG>(app.config.poll_seconds)*1000) { last=now; Inventory(); }
        return 0;
    }
    case Tray:
        if(lp==WM_LBUTTONUP || lp==WM_LBUTTONDBLCLK || lp==NIN_BALLOONUSERCLICK) Show();
        if(lp==WM_RBUTTONUP) {
            auto menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,1,L"Show information"); AppendMenuW(menu,MF_STRING,2,L"Exit (wait safely)");
            POINT p; GetCursorPos(&p); SetForegroundWindow(window);
            auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr); DestroyMenu(menu);
            if(choice==1) Show(); else if(choice==2) ExitApp();
        }
        return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==Disks && HIWORD(wp)==CBN_SELCHANGE) DisplayBudget();
        if(app.busy || app.closing) { if(LOWORD(wp)==Exit) ExitApp(); return 0; }
        switch(LOWORD(wp)) {
        case Save:SaveSettings();break;
        case Docker:Prepare(wc::Action::DockerCleanup);break;
        case Apt:Prepare(wc::Action::WslAptCache);break;
        case Compact:Prepare(wc::Action::Compact);break;
        case Execute:ExecutePlan();break;
        case Exit:ExitApp();break;
        }
        return 0;
    case Finished: {
        std::unique_ptr<Work> w;
        { std::lock_guard<std::mutex> lock(app.result_mutex); w=std::move(app.pending); }
        if(!w) return 0; // A timer fallback may already have consumed this notice.
        if(app.worker.joinable()) app.worker.join();
        app.busy=false;
        if(app.closing) { DestroyWindow(window); return 0; }
        if(!w->error.empty()) Error(w->error);
        else if(w->kind==Work::Inventory) {
            auto selected=SelectedDisk(); app.snapshot=std::move(w->snapshot);
            SendMessageW(Control(Disks),CB_RESETCONTENT,0,0); int index=0,chosen=0;
            for(const auto& d:app.snapshot.disks) {
                std::wstring label=d.kind+L": "+d.name+L" ["+d.id+L"]";
                SendMessageW(Control(Disks),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
                if(d.id==selected) chosen=index; ++index;
            }
            SendMessageW(Control(Disks),CB_SETCURSEL,chosen,0); DisplayBudget();
            if(!w->message.empty()) app.status=w->message;
            Render();
            if(!w->alerts.empty()) {
                std::wstring message;
                for(const auto& a:w->alerts) message+=a.message+L"\r\n";
                app.status+=L"\r\nALERT: "+message; Render();
                NOTIFYICONDATAW ni{}; ni.cbSize=sizeof(ni); ni.hWnd=window; ni.uID=1; ni.uFlags=NIF_INFO;
                lstrcpynW(ni.szInfoTitle,L"WSL Cleaner disk alert",64); lstrcpynW(ni.szInfo,message.c_str(),256); ni.dwInfoFlags=NIIF_WARNING;
                Shell_NotifyIconW(NIM_MODIFY,&ni);
                Show(); // Also visible when notifications are suppressed by Windows.
            }
        } else if(w->kind==Work::Prepared) {
            app.plan=std::move(w->plan); app.has_plan=true;
            std::wstring full=L"FULL PLAN (expires "+std::to_wstring(app.plan.expires)+L")\r\nTarget: "+app.plan.target+
                L"\r\nBackup folder: "+app.plan.backup_directory+L"\r\n";
            for(const auto& step:app.plan.steps) full+=step+L"\r\n";
            full+=L"\r\n";
            if(app.plan.action==wc::Action::Compact)
                full+=L"ALL WSL distributions and Docker workloads will stop. Requires administrator rights, verified consistent backup and exclusive access. Type COMPACT and tick AllowShutdown.";
            else full+=L"Deletion is irreversible. Only configured categories/age are allowed. Type CLEAN to approve.";
            Set(Report,Lines(full)); Show();
        } else { app.status=w->message; Render(); }
        EnableActions(); return 0;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wp),RGB(210,240,210)); SetBkColor(reinterpret_cast<HDC>(wp),RGB(0,0,0));
        return reinterpret_cast<LRESULT>(app.black);
    case WM_DESTROY:
        if(app.tray) { NOTIFYICONDATAW ni{}; ni.cbSize=sizeof(ni); ni.hWnd=window; ni.uID=1; Shell_NotifyIconW(NIM_DELETE,&ni); app.tray=false; }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}
LRESULT CALLBACK WindowProc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    try { return WindowProcImpl(window,msg,wp,lp); }
    catch(...) {
        app.has_plan=false;
        MessageBoxW(window,L"UI operation failed. No new maintenance will be started by this event. Use Exit to wait safely for any active operation.",L"WSL Cleaner",MB_OK|MB_ICONERROR);
        return 0;
    }
}
void Print(const std::wstring& s) {
    // Honour explicitly inherited redirected output before attaching a console.
    HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE);
    if(!out || out==INVALID_HANDLE_VALUE) {
        if(!AttachConsole(ATTACH_PARENT_PROCESS) && GetLastError()!=ERROR_ACCESS_DENIED) return;
        out=GetStdHandle(STD_OUTPUT_HANDLE);
    }
    DWORD written=0;
    if(!out || out==INVALID_HANDLE_VALUE) return;
    DWORD mode=0;
    if(GetConsoleMode(out,&mode)) WriteConsoleW(out,s.data(),static_cast<DWORD>(s.size()),&written,nullptr);
    else {
        int size=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
        std::string bytes(size,'\0'); WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),bytes.data(),size,nullptr,nullptr);
        WriteFile(out,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr);
    }
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv) return 2;
    std::wstring option=argc==2?argv[1]:L"";
    bool invalid=argc>2 || (argc==2 && option!=L"--help" && option!=L"--version" && option!=L"--inventory" && option!=L"--smoke-test" && option!=L"--tray");
    LocalFree(argv);
    if(invalid) { Print(L"Unknown arguments. Use --help.\r\n"); return 2; }
    if(option==L"--help") {
        Print(L"WSL Cleaner native\r\n--help  --version  --inventory  --smoke-test  --tray\r\nInventory is read-only. Smoke test creates controls then exits without settings, tray, polling or maintenance.\r\nNo flags bypass maintenance consent. Close hides; tray menu Exit waits for active work.\r\n"); return 0;
    }
    if(option==L"--version") { Print(L"WSL Cleaner native 1\r\n"); return 0; }
    if(option==L"--inventory") {
        try { Print(wc::FormatSnapshot(wc::Collect(),wc::Config{})); return 0; }
        catch(...) { Print(L"Inventory failed.\r\n"); return 1; }
    }
    app.smoke=option==L"--smoke-test";
    HANDLE mutex=nullptr;
    if(!app.smoke) {
        // SID, not a shared machine-wide name: one resident instance per user, across sessions.
        HANDLE token=nullptr; DWORD bytes=0; std::vector<BYTE> data;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return 1;
        GetTokenInformation(token,TokenUser,nullptr,0,&bytes); data.resize(bytes);
        if(!GetTokenInformation(token,TokenUser,data.data(),bytes,&bytes)) { CloseHandle(token); return 1; }
        CloseHandle(token);
        auto sid=reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;
        // Hex SID bytes avoid another library dependency and unsafe user-controlled names.
        std::wstring name=L"Global\\WslCleaner-";
        const wchar_t* hex=L"0123456789abcdef";
        for(DWORD i=0;i<GetLengthSid(sid);++i) { BYTE b=static_cast<BYTE*>(sid)[i]; name+=hex[b>>4]; name+=hex[b&15]; }
        mutex=CreateMutexW(nullptr,FALSE,name.c_str());
        if(!mutex) return 1;
        if(GetLastError()==ERROR_ALREADY_EXISTS) { CloseHandle(mutex); MessageBoxW(nullptr,L"WSL Cleaner is already running for this user. Open its tray icon.",L"WSL Cleaner",MB_OK); return 0; }
    }
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int code=0;
    try {
        if(FAILED(com)) throw std::runtime_error("COM initialization");
        if(!app.smoke) {
            app.config_path=wc::DataDirectory()+L"\\settings.conf";
            std::wstring error;
            app.valid=wc::LoadConfig(app.config_path,app.config,error);
            if(!app.valid) { app.config=wc::Config{}; app.status=L"CONFIG ERROR: "+error+L" Automation disabled until explicit valid Save. Existing store will not be overwritten automatically."; }
            else app.status=L"Monitoring. No destructive categories are enabled by default.";
        }
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
        WNDCLASSW klass{}; klass.lpfnWndProc=WindowProc; klass.hInstance=instance; klass.lpszClassName=L"WslCleanerNative";
        klass.hIcon=LoadIconW(nullptr,IDI_APPLICATION); klass.hCursor=LoadCursorW(nullptr,IDC_ARROW); klass.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        if(!RegisterClassW(&klass)) throw std::runtime_error("register window");
        app.window=CreateWindowExW(0,klass.lpszClassName,L"WSL Cleaner - advisory disk monitor",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            CW_USEDEFAULT,CW_USEDEFAULT,1000,725,nullptr,nullptr,instance,nullptr);
        if(!app.window) throw std::runtime_error("create window");
        Controls();
        if(app.smoke) { Print(L"SMOKE OK: native controls initialized; no tray, polling or maintenance.\r\n"); DestroyWindow(app.window); }
        else {
            NOTIFYICONDATAW ni{}; ni.cbSize=sizeof(ni); ni.hWnd=app.window; ni.uID=1;
            ni.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; ni.uCallbackMessage=Tray; ni.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
            lstrcpynW(ni.szTip,L"WSL Cleaner - advisory disk monitor",128);
            app.tray=Shell_NotifyIconW(NIM_ADD,&ni)!=FALSE;
            if(option!=L"--tray" || !app.tray || !app.valid) Show();
            if(!SetTimer(app.window,PollTimer,1000,nullptr)) throw std::runtime_error("polling timer");
            Inventory();
        }
        MSG message; int result;
        while((result=GetMessageW(&message,nullptr,0,0))>0) {
            if(!IsDialogMessageW(app.window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        if(result<0) code=1;
    } catch(...) { MessageBoxW(nullptr,L"Native application initialization failed.",L"WSL Cleaner",MB_OK|MB_ICONERROR); code=1; }
    // Also protects initialization/message-loop exception paths. No worker is detached.
    if(app.worker.joinable()) app.worker.join();
    if(IsWindow(app.window)) {
        DestroyWindow(app.window);
    }
    if(app.font) DeleteObject(app.font); if(app.black) DeleteObject(app.black);
    if(SUCCEEDED(com)) CoUninitialize(); if(mutex) CloseHandle(mutex);
    return code;
}
