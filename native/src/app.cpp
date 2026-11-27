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
#include <cwchar>
#include <functional>
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
       Auto, Cache, Dangling, Disposable, SaveDisk, SaveGlobal, Docker, Apt, Compact,
       Consent, Shutdown, Execute, Exit, Tabs, DiskPath, CopyPath, DiskLabel, PathLabel,
       SizeLabel, BudgetState, DiskState, BudgetLabel, DiskHelp, ReserveLabel, WarningLabel,
       CriticalLabel, PollLabel, CooldownLabel, AgeLabel, GlobalHelp, MaintenanceHelp,
       ConsentLabel, PlanView, Status, DiagnosticHelp };
enum Page { DiskPage, GlobalPage, MaintenancePage, DiagnosticPage };
struct PageControl { HWND window; int page; };
constexpr std::uint64_t GiB=1024ULL*1024*1024;
struct GlobalInput {
    std::wstring reserve, warning, critical, poll, cooldown, age;
    bool automatic=false, cache=false, dangling=false, disposable=false;
};
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
    std::wstring config_path, status, plan_text;
    std::wstring selected_id, budget_display=L"0", reserve_display;
    wc::VirtualDisk selected_disk;
    std::uint64_t budget_bytes=0, reserve_bytes=0;
    bool budget_dirty=false, setting_fields=false, smoke_failed=false;
    int page=DiskPage;
    std::vector<PageControl> page_controls;
    std::vector<std::wstring> choices;
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
std::wstring FormatGiB(std::uint64_t bytes) {
    std::wstring s=std::to_wstring(bytes/GiB);
    auto remainder=bytes%GiB;
    if(remainder) s+=L".";
    // Exact terminating decimal, at most 30 places; no float or rounding.
    while(remainder) { remainder*=10; s+=wchar_t(L'0'+remainder/GiB); remainder%=GiB; }
    return s;
}
std::wstring Quantity(std::uint64_t bytes) {
    const auto tenths=(bytes%GiB)*10/GiB;
    return std::to_wstring(bytes/GiB)+L","+std::to_wstring(tenths)+L" GiB ("+std::to_wstring(bytes)+L" bytes)";
}
bool Unsigned(const std::wstring& s,std::uint64_t& value) {
    if(s.empty()) return false;
    value=0;
    for(wchar_t c:s) {
        if(c<L'0'||c>L'9'||value>(UINT64_MAX-(c-L'0'))/10) return false;
        value=value*10+(c-L'0');
    }
    return true;
}
bool ParseGiB(const std::wstring& s,std::uint64_t& bytes) {
    if(s.empty() || s.size()>128) return false;
    auto point=s.find_first_of(L".,");
    auto wholeText=s.substr(0,point);
    std::uint64_t whole=0;
    if(!Unsigned(wholeText,whole) || whole>UINT64_MAX/GiB) return false;
    std::wstring fraction=point==std::wstring::npos?L"":s.substr(point+1);
    if(point!=std::wstring::npos && fraction.empty()) return false;
    for(wchar_t c:fraction) if(c<L'0'||c>L'9') return false;
    while(!fraction.empty() && fraction.back()==L'0') fraction.pop_back();
    if(fraction.size()>30) return false;
    // Decimal fraction times 2^30 using decimal digits, never floating point.
    std::uint64_t part=0;
    for(int bit=0;bit<30;++bit) {
        unsigned carry=0;
        for(size_t i=fraction.size();i>0;--i) {
            unsigned n=static_cast<unsigned>(fraction[i-1]-L'0')*2+carry;
            fraction[i-1]=wchar_t(L'0'+n%10); carry=n/10;
        }
        part=part*2+carry;
    }
    for(wchar_t c:fraction) if(c!=L'0') return false; // fractional byte
    const auto base=whole*GiB;
    if(part>UINT64_MAX-base) return false;
    bytes=base+part; return true;
}
bool InputBytes(const std::wstring& text,const std::wstring& original,std::uint64_t bytes,
                std::uint64_t& result,std::wstring& error) {
    if(text==original) { result=bytes; return true; }
    if(ParseGiB(text,result)) return true;
    error=L"Valor GiB inválido: use dígitos e uma vírgula ou ponto decimal, sem separadores de milhar. O valor deve corresponder a bytes inteiros e caber em 64 bits.";
    return false;
}
// Candidates are pure: no UI, service, file or mutation of current on rejection.
bool DiskBudgetCandidate(const wc::Config& current,const std::wstring& id,const std::wstring& input,
                         const std::wstring& original,std::uint64_t originalBytes,
                         wc::Config& candidate,std::wstring& error) {
    if(id.empty()) { error=L"Selecione um disco medido primeiro."; return false; }
    std::uint64_t bytes=0;
    if(!InputBytes(input,original,originalBytes,bytes,error)) return false;
    wc::Config c=current;
    if(bytes) c.budgets[id]=bytes; else c.budgets.erase(id);
    if(!wc::ValidateConfig(c,error)) return false;
    candidate=std::move(c); return true;
}
bool GlobalSettingsCandidate(const wc::Config& current,const GlobalInput& input,
                             const std::wstring& original,std::uint64_t originalBytes,
                             wc::Config& candidate,std::wstring& error) {
    wc::Config c=current;
    if(!InputBytes(input.reserve,original,originalBytes,c.host_reserve,error)) return false;
    std::uint64_t values[5]{};
    const std::wstring texts[]={input.warning,input.critical,input.poll,input.cooldown,input.age};
    for(int i=0;i<5;++i) if(!Unsigned(texts[i],values[i]) || values[i]>MAXDWORD) {
        error=L"Percentuais, intervalos e idade exigem inteiros sem sinal dentro do intervalo permitido."; return false;
    }
    c.warning_percent=static_cast<unsigned>(values[0]); c.critical_percent=static_cast<unsigned>(values[1]);
    c.poll_seconds=static_cast<unsigned>(values[2]); c.cooldown_seconds=static_cast<unsigned>(values[3]);
    c.minimum_age_days=static_cast<unsigned>(values[4]);
    c.auto_cleanup=input.automatic; c.clean_build_cache=input.cache;
    c.clean_dangling_images=input.dangling; c.clean_disposable_containers=input.disposable;
    if(!wc::ValidateConfig(c,error)) return false;
    candidate=std::move(c); return true;
}
void RenderDiagnostics() {
    std::wstring text=L"Comprimento do arquivo VHDX somente; alocação física e uso no convidado desconhecidos.\r\n"
        L"Limites são alertas, não cotas. Mensagens técnicas originais podem estar em inglês.\r\n"+app.status+L"\r\n\r\nCandidatos com medição indisponível:\r\n";
    for(const auto& d:app.snapshot.disks) if(!d.measured) text+=d.name+L"\r\n"+d.path+L"\r\n";
    text+=L"Indisponível não prova que o arquivo esteja ausente, seja lixo ou possa ser apagado.\r\n\r\nRELATÓRIO TÉCNICO ORIGINAL\r\n";
    Set(Report,Lines(text+wc::FormatSnapshot(app.snapshot,app.config)));
}
void RenderDisk() {
    const auto found=std::find_if(app.snapshot.disks.begin(),app.snapshot.disks.end(),
        [](const wc::VirtualDisk& d) { return d.id==app.selected_id && d.measured; });
    const bool available=found!=app.snapshot.disks.end();
    if(available) app.selected_disk=*found;
    Set(DiskPath,app.selected_id.empty()?L"Nenhum disco medido selecionado.":app.selected_disk.path);
    Set(SizeLabel,available?L"Tamanho do arquivo: "+Quantity(found->length):L"Tamanho do arquivo: medição indisponível");
    const auto budget=app.config.budgets.find(app.selected_id);
    Set(BudgetState,budget==app.config.budgets.end()?L"Limite salvo: não definido":L"Limite salvo: "+Quantity(budget->second));
    std::wstring state;
    if(app.selected_id.empty()) state=L"Estado: nenhum disco medido. Consulte Diagnóstico.";
    else if(!available) state=L"Estado: indisponível — alvo e edição preservados: "+app.selected_disk.name;
    else if(budget==app.config.budgets.end()) state=L"Estado: medido, sem limite de alerta";
    else {
        // Avoid uint64 multiplication overflow when comparing percentages.
        auto threshold=[&](unsigned p) { return (budget->second/100)*p+((budget->second%100)*p+99)/100; };
        state=found->length>=threshold(app.config.critical_percent)?L"Estado: CRÍTICO":
              found->length>=threshold(app.config.warning_percent)?L"Estado: AVISO":L"Estado: normal";
    }
    if(app.budget_dirty) state+=L" · limite alterado, ainda não salvo";
    Set(DiskState,state);
}
void Render() { Set(Status,app.status); RenderDisk(); RenderDiagnostics(); }
void Error(const std::wstring& s) { app.status=L"ERRO: "+s; Render(); if(!app.smoke) Show(); }
void EnableActions() {
    const bool ready=!app.busy && !app.closing;
    for(int id : {Disks,Budget,Reserve,Warning,Critical,Poll,Cooldown,Age,Auto,Cache,Dangling,Disposable,
                  SaveDisk,SaveGlobal,Docker,Apt,Compact,Consent,Shutdown}) EnableWindow(Control(id),ready);
    EnableWindow(Control(SaveDisk),ready && !app.selected_id.empty());
    EnableWindow(Control(Execute),ready && app.has_plan);
    EnableWindow(Control(Exit),!app.closing);
}
template<class F> void Start(F action) {
    if(app.busy || app.closing) return;
    if(app.worker.joinable()) app.worker.join();
    app.busy=true; EnableActions();
    try {
        HWND window=app.window;
        app.worker=std::thread([window,action,result=std::make_unique<Work>()]() mutable {
            try { action(*result); }
            catch(const std::exception&) { result->error=L"Falha em operação de segundo plano (exceção C++)."; }
            catch(...) { result->error=L"Falha em operação de segundo plano (exceção desconhecida)."; }
            {
                std::lock_guard<std::mutex> lock(app.result_mutex);
                app.pending=std::move(result);
            }
            // Ownership stays in a guarded slot, not an untracked message pointer.
            // A live timer also drains the slot if notification posting fails.
            PostMessageW(window,Finished,0,0);
        });
    } catch(...) { app.busy=false; EnableActions(); Error(L"Não foi possível iniciar a operação de segundo plano."); }
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
                w.message=(r.success?L"Limpeza automática Docker concluída: ":L"Limpeza automática Docker FALHOU: ")+r.message;
            } else w.message=L"Falha na limpeza automática Docker: "+error;
        }
    });
}
std::wstring SelectedDisk() {
    auto i=SendMessageW(Control(Disks),CB_GETCURSEL,0,0);
    if(i!=CB_ERR) {
        auto key=SendMessageW(Control(Disks),CB_GETITEMDATA,i,0);
        if(key>=0 && static_cast<size_t>(key)<app.choices.size()) return app.choices[static_cast<size_t>(key)];
    }
    return app.selected_id;
}
void SelectIdentity(const std::wstring& id) {
    int selected=-1;
    const auto count=SendMessageW(Control(Disks),CB_GETCOUNT,0,0);
    for(int i=0;i<count;++i) {
        auto key=SendMessageW(Control(Disks),CB_GETITEMDATA,i,0);
        if(key>=0 && static_cast<size_t>(key)<app.choices.size() && app.choices[static_cast<size_t>(key)]==id) selected=i;
    }
    SendMessageW(Control(Disks),CB_SETCURSEL,selected,0);
}
void LoadSelectedBudget() {
    const auto it=app.config.budgets.find(app.selected_id);
    app.budget_bytes=it==app.config.budgets.end()?0:it->second;
    app.budget_display=FormatGiB(app.budget_bytes);
    app.setting_fields=true; Set(Budget,app.budget_display); app.setting_fields=false;
    app.budget_dirty=false;
}
void InvalidatePlan() {
    app.has_plan=false; app.plan_text=L"Nenhum plano válido. Prepare e revise um novo plano antes de consentir.";
    Set(PlanView,app.plan_text); Set(Consent,L""); Check(Shutdown,false); EnableActions();
}
void RefreshDiskChoices() {
    const auto selected=app.selected_id.empty()?SelectedDisk():app.selected_id;
    SendMessageW(Control(Disks),CB_RESETCONTENT,0,0); app.choices.clear();
    for(const auto& d:app.snapshot.disks) {
        if(!d.measured) continue;
        const auto key=app.choices.size(); app.choices.push_back(d.id);
        auto label=d.kind+L": "+d.name;
        auto index=SendMessageW(Control(Disks),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        if(index==CB_ERR || index==CB_ERRSPACE || SendMessageW(Control(Disks),CB_SETITEMDATA,index,key)==CB_ERR)
            throw std::runtime_error("disk choice");
    }
    // An unavailable selection stays an explicit retained target, not an invented measured item.
    app.selected_id=selected;
    if(app.selected_id.empty() && !app.choices.empty()) {
        app.selected_id=app.choices.front(); LoadSelectedBudget();
    }
    SelectIdentity(app.selected_id); RenderDisk(); EnableActions();
}
enum class DiscardDecision { Ask, Keep, Discard };
// Private smoke seam models messages dispatched inside MessageBox, at the same
// point after next is captured and before the returned decision is applied.
// It never supplies maintenance approval and is never invoked in resident mode.
void ChangeDiskSelection(DiscardDecision decision=DiscardDecision::Ask,
                         const std::function<DiscardDecision()>& smokePrompt=nullptr) {
    const auto next=SelectedDisk();
    if(next==app.selected_id) return;
    if(app.budget_dirty) {
        if(decision==DiscardDecision::Ask) {
            if(app.smoke) {
                if(!smokePrompt) throw std::runtime_error("smoke must not open a consent dialog");
                decision=smokePrompt();
            } else {
                decision=MessageBoxW(app.window,L"Descartar o limite não salvo do disco atual e trocar de disco?",
                    L"Alteração não salva",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)==IDYES?DiscardDecision::Discard:DiscardDecision::Keep;
            }
        }
        if(decision==DiscardDecision::Keep) { SelectIdentity(app.selected_id); return; }
    }
    // The prompt pumps messages: refresh may reorder/remove next and restore the
    // combo to the old selection. Resolve the captured ID against fresh inventory,
    // never a pre-dialog index, before committing or discarding any input.
    const auto target=std::find_if(app.snapshot.disks.begin(),app.snapshot.disks.end(),
        [&](const wc::VirtualDisk& disk) { return disk.id==next && disk.measured; });
    if(target==app.snapshot.disks.end()) {
        SelectIdentity(app.selected_id);
        app.status=L"O disco escolhido ficou indisponível durante a troca. A seleção anterior e suas edições foram mantidas.";
        Render(); return;
    }
    app.selected_id=target->id; app.selected_disk=*target;
    SelectIdentity(app.selected_id);
    LoadSelectedBudget(); InvalidatePlan(); Render();
}
bool Persist(const wc::Config& candidate) {
    std::wstring error;
    if(!wc::ValidateConfig(candidate,error) || !wc::SaveConfig(app.config_path,candidate,error)) { Error(error); return false; }
    app.config=candidate; app.valid=true; InvalidatePlan(); return true;
}
void SaveDiskBudget() {
    wc::Config candidate; std::wstring error;
    if(!DiskBudgetCandidate(app.config,app.selected_id,Text(Budget),app.budget_display,app.budget_bytes,candidate,error)) { Error(error); return; }
    if(!Persist(candidate)) return;
    LoadSelectedBudget(); app.status=L"Limite deste disco salvo. Configurações globais e limites de outros discos preservados.";
    Render(); EnableActions();
}
GlobalInput ReadGlobalInput() {
    return {Text(Reserve),Text(Warning),Text(Critical),Text(Poll),Text(Cooldown),Text(Age),
            Checked(Auto),Checked(Cache),Checked(Dangling),Checked(Disposable)};
}
void SaveGlobalSettings() {
    wc::Config candidate; std::wstring error;
    if(!GlobalSettingsCandidate(app.config,ReadGlobalInput(),app.reserve_display,app.reserve_bytes,candidate,error)) { Error(error); return; }
    if(!Persist(candidate)) return;
    app.reserve_bytes=app.config.host_reserve; app.reserve_display=Text(Reserve);
    app.status=L"Configurações gerais salvas. Todos os limites individuais preservados; edição do disco mantida.";
    KillTimer(app.window,PollTimer);
    // Elapsed check uses 64-bit monotonic time; never truncate configured seconds.
    if(!SetTimer(app.window,PollTimer,1000,nullptr)) {
        Error(L"Configurações salvas, mas o temporizador falhou. Saia e reinicie o aplicativo."); EnableActions(); return;
    }
    Render(); EnableActions();
}
std::wstring Folder() {
    BROWSEINFOW bi{}; bi.hwndOwner=app.window; bi.lpszTitle=L"Escolha pasta de cópia existente em OUTRO volume fixo (nunca a pasta do disco de origem)";
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
    if(!app.valid) { Error(L"Salve configurações válidas explicitamente antes da manutenção. Arquivos inválidos não são sobrescritos automaticamente."); return; }
    std::wstring target=L"docker";
    if(action!=wc::Action::DockerCleanup) {
        const auto selected=app.selected_id;
        const auto found=std::find_if(app.snapshot.disks.begin(),app.snapshot.disks.end(),
            [&](const wc::VirtualDisk& item) { return item.id==selected; });
        if(found==app.snapshot.disks.end() || !found->measured) { Error(L"Selecione um disco com medição disponível. Consulte Diagnóstico."); return; }
        const auto* disk=&*found;
        if(action==wc::Action::WslAptCache) {
            if(disk->kind!=L"WSL") { Error(L"Selecione uma distribuição WSL2 registrada que não seja Docker para limpar o cache apt."); return; }
            target=disk->name;
        } else target=disk->path;
    }
    std::wstring backup;
    if(action==wc::Action::Compact) {
        if(!IsAdmin()) {
            Error(L"Compactação exige administrador. Saia desta instância e abra explicitamente como administrador. Não há elevação automática."); return;
        }
        backup=Folder(); if(backup.empty()) return;
    }
    InvalidatePlan(); app.plan_text=L"Preparando plano com as configurações SALVAS. Aguarde e revise antes de aprovar.";
    Set(PlanView,app.plan_text);
    auto config=app.config;
    Start([action,target,backup,config](Work& w) {
        w.kind=Work::Prepared;
        if(!app.maintenance.Prepare(action,target,config,backup,w.plan,w.error) && w.error.empty()) w.error=L"Falha ao preparar plano.";
    });
}
void ExecutePlan() {
    if(!app.has_plan) return;
    const bool compact=app.plan.action==wc::Action::Compact;
    if(Text(Consent)!=(compact?L"COMPACT":L"CLEAN")) { Error(L"Digite exatamente CLEAN ou COMPACT conforme o plano. Prepare um novo plano para revisar."); InvalidatePlan(); return; }
    if(compact && !Checked(Shutdown)) { Error(L"Compactação exige autorizar a parada de TODAS as distribuições WSL e cargas Docker. Prepare novo plano."); InvalidatePlan(); return; }
    const auto plan=app.plan; const auto config=app.config;
    const wc::Approval approval{plan.token,Text(Consent),Checked(Shutdown)};
    app.has_plan=false;
    Start([plan,config,approval](Work& w) {
        w.kind=Work::Executed;
        auto result=app.maintenance.Execute(plan,approval,config);
        if(result.success) w.message=result.message; else w.error=result.message;
    });
}
void Child(const wchar_t* cls,const wchar_t* text,int id,int x,int y,int width,int height,DWORD style=0,int page=-1) {
    const bool focusable=wcscmp(cls,L"STATIC")!=0;
    auto h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style|(focusable?WS_TABSTOP:0),x,y,width,height,app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    if(!h) throw std::runtime_error("control creation");
    SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(app.font),TRUE);
    if(page>=0) app.page_controls.push_back({h,page});
}
void SetPage(int page) {
    app.page=page; TabCtrl_SetCurSel(Control(Tabs),page);
    for(const auto& item:app.page_controls) ShowWindow(item.window,item.page==page?SW_SHOW:SW_HIDE);
}
void Place(int id,int x,int y,int width,int height) {
    MoveWindow(Control(id),x,y,std::max(1,width),std::max(1,height),TRUE);
}
void Layout(int width,int height) {
    if(!Control(Tabs)) return;
    Place(Tabs,12,8,width-24,36);
    const int left=20,usable=width-40;
    Place(DiskLabel,left,58,usable,24);
    MoveWindow(Control(Disks),20,84,width-40,260,TRUE);
    Place(PathLabel,left,124,usable,24); Place(DiskPath,left,152,usable,64);
    Place(CopyPath,left,228,180,32);
    Place(SizeLabel,left,274,usable,36); Place(BudgetState,left,318,usable,36);
    Place(DiskState,left,362,usable,48);
    Place(BudgetLabel,left,422,usable,24); Place(Budget,left,452,260,30);
    Place(SaveDisk,left,494,320,34); Place(DiskHelp,left,542,usable,66);
    const int fields[]={Reserve,Warning,Critical,Poll,Cooldown,Age};
    const int labels[]={ReserveLabel,WarningLabel,CriticalLabel,PollLabel,CooldownLabel,AgeLabel};
    const int col=(usable-24)/3;
    for(int i=0;i<6;++i) {
        int x=left+(i%3)*(col+12),y=64+(i/3)*88;
        Place(labels[i],x,y,col,42); Place(fields[i],x,y+44,col,30);
    }
    Place(Auto,left,240,usable,40); Place(Cache,left,288,usable,30);
    Place(Dangling,left,326,usable,30); Place(Disposable,left,364,usable,44);
    Place(SaveGlobal,left,426,320,34); Place(GlobalHelp,left,474,usable,136);
    Place(MaintenanceHelp,left,58,usable,108);
    const int buttonWidth=(usable-16)/3;
    Place(Docker,left,174,buttonWidth,44); Place(Apt,left+buttonWidth+8,174,buttonWidth,44);
    Place(Compact,left+2*(buttonWidth+8),174,buttonWidth,44);
    const int consentY=height-268;
    Place(PlanView,left,232,usable,consentY-244);
    Place(ConsentLabel,left,consentY,usable,24); Place(Consent,left,consentY+30,220,30);
    Place(Shutdown,left,consentY+68,usable,48); Place(Execute,left,consentY+124,340,34);
    Place(DiagnosticHelp,left,58,usable,72); Place(Report,left,140,usable,height-252);
    Place(Status,20,height-100,usable,52); Place(Exit,width-350,height-42,330,32);
}
void Controls() {
    app.font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,FIXED_PITCH,L"Consolas");
    app.black=CreateSolidBrush(RGB(0,0,0));
    Child(WC_TABCONTROL,L"",Tabs,0,0,1,1,TCS_SINGLELINE);
    const wchar_t* pages[]={L"Discos",L"Configurações gerais",L"Manutenção",L"Diagnóstico"};
    for(int i=0;i<4;++i) { TCITEMW item{}; item.mask=TCIF_TEXT; item.pszText=const_cast<wchar_t*>(pages[i]); if(TabCtrl_InsertItem(Control(Tabs),i,&item)==-1) throw std::runtime_error("tab creation"); }
    Child(L"STATIC",L"Disco selecionado — somente discos com medição disponível",DiskLabel,0,0,1,1,0,DiskPage);
    Child(L"COMBOBOX",L"",Disks,0,0,1,1,CBS_DROPDOWNLIST|WS_VSCROLL,DiskPage);
    Child(L"STATIC",L"Caminho completo (somente leitura; Ctrl+C também copia a seleção)",PathLabel,0,0,1,1,0,DiskPage);
    Child(L"EDIT",L"",DiskPath,0,0,1,1,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER,DiskPage);
    Child(L"BUTTON",L"Copiar caminho",CopyPath,0,0,1,1,0,DiskPage);
    Child(L"STATIC",L"",SizeLabel,0,0,1,1,0,DiskPage); Child(L"STATIC",L"",BudgetState,0,0,1,1,0,DiskPage);
    Child(L"STATIC",L"",DiskState,0,0,1,1,0,DiskPage);
    Child(L"STATIC",L"Limite de alerta (GiB; 0 = sem limite)",BudgetLabel,0,0,1,1,0,DiskPage);
    Child(L"EDIT",L"0",Budget,0,0,1,1,WS_BORDER|ES_AUTOHSCROLL,DiskPage);
    Child(L"BUTTON",L"Salvar limite deste disco",SaveDisk,0,0,1,1,0,DiskPage);
    Child(L"STATIC",L"Comprimento do arquivo, NÃO uso dentro do Linux nem alocação física. Limites são alertas, não cotas. Atualizações preservam edições e planos. Caminhos indisponíveis ficam em Diagnóstico; limites de IDs não vistos são mantidos.",DiskHelp,0,0,1,1,0,DiskPage);
    const wchar_t* labels[]={L"Reserva livre (GiB; 0 desativa)",L"Aviso (%)",L"Crítico (%)",L"Atualização (segundos)",L"Intervalo entre alertas (s)",L"Idade mínima Docker (dias)"};
    const int fields[]={Reserve,Warning,Critical,Poll,Cooldown,Age};
    const int labelIds[]={ReserveLabel,WarningLabel,CriticalLabel,PollLabel,CooldownLabel,AgeLabel};
    for(int i=0;i<6;++i) {
        Child(L"STATIC",labels[i],labelIds[i],0,0,1,1,0,GlobalPage);
        Child(L"EDIT",L"",fields[i],0,0,1,1,WS_BORDER|ES_AUTOHSCROLL,GlobalPage);
    }
    Child(L"BUTTON",L"Permitir limpeza automática após alerta de limite Docker",Auto,0,0,1,1,BS_AUTOCHECKBOX|BS_MULTILINE,GlobalPage);
    Child(L"BUTTON",L"Cache de compilação antigo e sem uso",Cache,0,0,1,1,BS_AUTOCHECKBOX,GlobalPage);
    Child(L"BUTTON",L"Imagens antigas sem referência e sem uso",Dangling,0,0,1,1,BS_AUTOCHECKBOX,GlobalPage);
    Child(L"BUTTON",L"Somente contêineres parados com rótulo wsl-cleaner.disposable=true",Disposable,0,0,1,1,BS_AUTOCHECKBOX|BS_MULTILINE,GlobalPage);
    Child(L"BUTTON",L"Salvar configurações gerais",SaveGlobal,0,0,1,1,0,GlobalPage);
    Child(L"STATIC",L"Globais: não pertencem ao disco selecionado. Reserva vale para volumes fixos do computador (inclusive pequenos volumes de sistema); não aloca espaço. Aviso < crítico. Docker afeta o serviço local inteiro. Categorias também valem para planos manuais; marcar categoria não ativa limpeza automática. Exclusão irreversível e possivelmente parcial; volumes e contêineres comuns ficam excluídos. Não há limpeza WSL nem compactação automática. Valores GiB aceitam ponto ou vírgula, sem separadores de milhar e sem frações de byte.",GlobalHelp,0,0,1,1,0,GlobalPage);
    Child(L"STATIC",L"Preparar não executa. Docker: serviço local inteiro, independente da seleção. WSL: distribuição registrada pelo nome; apt-get clean pode iniciar essa distribuição como root. Compactação: VHDX selecionado; exige administrador, cópia integral verificada em OUTRO volume fixo e parada de TODAS as distribuições WSL e cargas Docker. Usam configurações SALVAS.",MaintenanceHelp,0,0,1,1,0,MaintenancePage);
    Child(L"BUTTON",L"Preparar limpeza Docker",Docker,0,0,1,1,BS_MULTILINE,MaintenancePage);
    Child(L"BUTTON",L"Preparar cache WSL",Apt,0,0,1,1,BS_MULTILINE,MaintenancePage);
    Child(L"BUTTON",L"Preparar compactação",Compact,0,0,1,1,BS_MULTILINE,MaintenancePage);
    Child(L"EDIT",L"Nenhum plano preparado. Revise um plano completo antes de aprovar.",PlanView,0,0,1,1,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER,MaintenancePage);
    Child(L"STATIC",L"Confirmação exata: CLEAN (limpeza) ou COMPACT (compactação)",ConsentLabel,0,0,1,1,0,MaintenancePage);
    Child(L"EDIT",L"",Consent,0,0,1,1,WS_BORDER|ES_AUTOHSCROLL,MaintenancePage);
    Child(L"BUTTON",L"Autorizar parada de TODAS as distribuições WSL e cargas Docker (somente compactação)",Shutdown,0,0,1,1,BS_AUTOCHECKBOX|BS_MULTILINE,MaintenancePage);
    Child(L"BUTTON",L"Executar plano revisado",Execute,0,0,1,1,0,MaintenancePage);
    Child(L"STATIC",L"Diagnóstico: candidatos sem medição e relatório técnico original. Indisponível não significa ausente, lixo ou seguro para apagar. Mensagens técnicas podem estar em inglês.",DiagnosticHelp,0,0,1,1,0,DiagnosticPage);
    Child(L"EDIT",L"",Report,0,0,1,1,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER,DiagnosticPage);
    Child(L"STATIC",L"",Status,0,0,1,1);
    Child(L"BUTTON",L"Sair (aguarda manutenção)",Exit,0,0,1,1);
    app.reserve_bytes=app.config.host_reserve; app.reserve_display=FormatGiB(app.reserve_bytes);
    Set(Reserve,app.reserve_display); Set(Warning,std::to_wstring(app.config.warning_percent));
    Set(Critical,std::to_wstring(app.config.critical_percent)); Set(Poll,std::to_wstring(app.config.poll_seconds));
    Set(Cooldown,std::to_wstring(app.config.cooldown_seconds)); Set(Age,std::to_wstring(app.config.minimum_age_days));
    Check(Auto,app.config.auto_cleanup); Check(Cache,app.config.clean_build_cache);
    Check(Dangling,app.config.clean_dangling_images); Check(Disposable,app.config.clean_disposable_containers);
    RECT r{}; GetClientRect(app.window,&r); Layout(r.right,r.bottom); SetPage(DiskPage);
    EnableActions(); Render();
}
std::wstring CopyPathText() { return app.selected_id.empty()?L"":app.selected_disk.path; }
void CopyDiskPath() {
    const auto path=CopyPathText();
    if(path.empty()) { Error(L"Nenhum caminho selecionado para copiar."); return; }
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(path.size()+1)*sizeof(wchar_t));
    if(!memory) { Error(L"Não foi possível reservar memória para copiar."); return; }
    auto data=static_cast<wchar_t*>(GlobalLock(memory));
    if(!data) { GlobalFree(memory); Error(L"Não foi possível preparar a cópia."); return; }
    std::copy(path.c_str(),path.c_str()+path.size()+1,data); GlobalUnlock(memory);
    if(!OpenClipboard(app.window)) { GlobalFree(memory); Error(L"Área de transferência ocupada; selecione o caminho e use Ctrl+C."); return; }
    const bool ok=EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;
    CloseClipboard(); if(!ok) GlobalFree(memory);
    app.status=ok?L"Caminho completo copiado.":L"Falha ao copiar caminho; use a seleção de texto e Ctrl+C."; Set(Status,app.status);
}
void SmokeRequire(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
bool SameGlobals(const wc::Config& a,const wc::Config& b) {
    return a.host_reserve==b.host_reserve && a.warning_percent==b.warning_percent && a.critical_percent==b.critical_percent &&
        a.poll_seconds==b.poll_seconds && a.cooldown_seconds==b.cooldown_seconds && a.minimum_age_days==b.minimum_age_days &&
        a.auto_cleanup==b.auto_cleanup && a.clean_build_cache==b.clean_build_cache &&
        a.clean_dangling_images==b.clean_dangling_images && a.clean_disposable_containers==b.clean_disposable_containers;
}
RECT ChildRect(int id) {
    RECT r{}; SmokeRequire(GetWindowRect(Control(id),&r)!=FALSE,"control rectangle");
    MapWindowPoints(nullptr,app.window,reinterpret_cast<POINT*>(&r),2); return r;
}
void HiddenPanelChecks() {
    SmokeRequire(app.smoke && !app.smoke_failed,"hidden initialization");
    SmokeRequire((GetWindowLongPtrW(app.window,GWL_STYLE)&WS_VISIBLE)==0,"smoke window must stay hidden");
    const SIZE sizes[]={{760,720},{980,820},{1280,980}};
    const std::wstring longPath=L"C:\\Users\\UsuárioExemplo\\AppData\\Local\\Packages\\DistribuiçãoUbuntuComNomeLongoParaVerificarQuebraSemRecorte\\LocalState\\DiretórioDeExemploComNomeLongo\\"+std::wstring(160,L'x')+L"\\ext4.vhdx";
    for(const auto& size:sizes) {
        RECT outer{0,0,size.cx,size.cy};
        SmokeRequire(AdjustWindowRectEx(&outer,static_cast<DWORD>(GetWindowLongPtrW(app.window,GWL_STYLE)),FALSE,0)!=FALSE,"adjust rectangle");
        SmokeRequire(SetWindowPos(app.window,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"resize hidden window");
        RECT client{}; GetClientRect(app.window,&client);
        SmokeRequire(client.right==size.cx && client.bottom==size.cy,"requested client size");
        // WM_SIZE must actually lay out controls; this test never calls Layout directly.
        auto combo=ChildRect(Disks),path=ChildRect(DiskPath),copy=ChildRect(CopyPath);
        SmokeRequire(combo.left==20 && combo.right==client.right-20,"full width disk selector");
        SmokeRequire(path.left==combo.left && path.right==combo.right && path.top>combo.top+24 && path.bottom>path.top+40,"separate wrapped path geometry");
        SmokeRequire(copy.top>=path.bottom && copy.right<=client.right-20,"path copy geometry");
        Set(DiskPath,longPath);
        SmokeRequire(Text(DiskPath)==longPath && SendMessageW(Control(DiskPath),EM_GETLINECOUNT,0,0)>1,"full Unicode path retained and actually wrapped");
        const auto pathStyle=GetWindowLongPtrW(Control(DiskPath),GWL_STYLE);
        SmokeRequire((pathStyle&(ES_MULTILINE|ES_READONLY))==(ES_MULTILINE|ES_READONLY) && !(pathStyle&ES_AUTOHSCROLL),"path wrapping and read-only styles");
        SmokeRequire(Text(CopyPath)==L"Copiar caminho" && IsWindowEnabled(Control(CopyPath)),"copy capability without clipboard mutation");
        SmokeRequire(TabCtrl_GetItemCount(Control(Tabs))==4,"four native tabs");
        const wchar_t* labels[]={L"Discos",L"Configurações gerais",L"Manutenção",L"Diagnóstico"};
        for(int page=0;page<4;++page) {
            wchar_t label[80]{}; TCITEMW item{}; item.mask=TCIF_TEXT; item.pszText=label; item.cchTextMax=80;
            SmokeRequire(TabCtrl_GetItem(Control(Tabs),page,&item)!=FALSE && std::wstring(label)==labels[page],"Portuguese tab label");
            TabCtrl_SetCurSel(Control(Tabs),page);
            NMHDR notification{}; notification.hwndFrom=Control(Tabs); notification.idFrom=Tabs; notification.code=TCN_SELCHANGE;
            SendMessageW(app.window,WM_NOTIFY,Tabs,reinterpret_cast<LPARAM>(&notification));
            SmokeRequire(app.page==page,"native tab notification");
            for(const auto& child:app.page_controls) {
                const auto style=GetWindowLongPtrW(child.window,GWL_STYLE);
                SmokeRequire(((style&WS_VISIBLE)!=0)==(child.page==page),"only active page children visible");
                if(child.page==page) {
                    RECT r{}; GetWindowRect(child.window,&r); MapWindowPoints(nullptr,app.window,reinterpret_cast<POINT*>(&r),2);
                    // COMBOBOX height includes its drop-down, which is not a page surface.
                    SmokeRequire(r.left>=20 && r.right<=client.right-20 && r.top>=58 && r.bottom<=client.bottom-108,"page control bounds");
                }
            }
            const auto plan=ChildRect(PlanView),consent=ChildRect(ConsentLabel);
            SmokeRequire(plan.bottom<consent.top && plan.bottom-plan.top>=200,"scrollable plan separate from consent");
        }
    }
    SetPage(DiskPage);
    wc::Config current; current.budgets={{L"alpha",GiB+1},{L"unseen",77}};
    current.auto_cleanup=true; current.clean_build_cache=true;
    wc::Config candidate,unchanged; unchanged.host_reserve=123; candidate=unchanged;
    std::wstring error; std::uint64_t parsed=0;
    const std::uint64_t roundTrips[]={0,1,GiB+1,UINT64_MAX};
    for(auto bytes:roundTrips)
        SmokeRequire(ParseGiB(FormatGiB(bytes),parsed) && parsed==bytes,"exact GiB round trip");
    SmokeRequire(ParseGiB(L"1,5",parsed) && parsed==GiB+GiB/2,"comma GiB");
    for(const auto* invalid:{L"",L"-1",L"1,000.5",L"0.1",L"17179869184",L"1.",L"1 000"})
        SmokeRequire(!ParseGiB(invalid,parsed),"invalid GiB rejected");
    SmokeRequire(!DiskBudgetCandidate(current,L"alpha",L"0.1",L"old",1,candidate,error) &&
                 SameGlobals(candidate,unchanged) && candidate.budgets==unchanged.budgets,"rejection preserves candidate");
    SmokeRequire(DiskBudgetCandidate(current,L"alpha",L"2.5",L"old",1,candidate,error) &&
                 SameGlobals(candidate,current) && candidate.budgets.at(L"alpha")==2*GiB+GiB/2 && candidate.budgets.at(L"unseen")==77,"disk scope merge");
    const auto originalBudget=FormatGiB(GiB+1);
    SmokeRequire(DiskBudgetCandidate(current,L"alpha",originalBudget,originalBudget,GiB+1,candidate,error) && candidate.budgets.at(L"alpha")==GiB+1,"original bytes unchanged");
    SmokeRequire(DiskBudgetCandidate(current,L"alpha",L"0",L"old",1,candidate,error) && candidate.budgets.count(L"alpha")==0 && candidate.budgets.at(L"unseen")==77,"zero removes only selected budget");
    GlobalInput globals{L"25.5",L"75",L"95",L"180",L"60",L"45",false,false,true,true};
    SmokeRequire(GlobalSettingsCandidate(current,globals,L"20",20*GiB,candidate,error) && candidate.budgets==current.budgets &&
        candidate.host_reserve==25*GiB+GiB/2 && candidate.warning_percent==75 && candidate.critical_percent==95 &&
        candidate.poll_seconds==180 && candidate.cooldown_seconds==60 && candidate.minimum_age_days==45 &&
        !candidate.auto_cleanup && !candidate.clean_build_cache && candidate.clean_dangling_images && candidate.clean_disposable_containers,"global scope merge");
    const auto originalReserve=FormatGiB(20*GiB+1); globals.reserve=originalReserve;
    SmokeRequire(GlobalSettingsCandidate(current,globals,originalReserve,20*GiB+1,candidate,error) && candidate.host_reserve==20*GiB+1,"reserve exact original bytes");
    globals.warning=L"100"; candidate=unchanged;
    SmokeRequire(!GlobalSettingsCandidate(current,globals,originalReserve,20*GiB+1,candidate,error) &&
        SameGlobals(candidate,unchanged) && candidate.budgets==unchanged.budgets,"invalid globals leave output untouched");
    SmokeRequire(current.budgets.at(L"alpha")==GiB+1 && current.budgets.at(L"unseen")==77,"current not mutated by candidates");
    // Exercise real controls and refresh/selection helpers using in-memory inventory only.
    app.config=current;
    const wc::VirtualDisk missing{L"candidate",L"Candidato",L"Docker",L"C:\\example\\missing.vhdx",0,false};
    const wc::VirtualDisk alpha{L"alpha",L"Ubuntu",L"WSL",L"C:\\example\\long-folder\\ext4.vhdx",42*GiB,true};
    const wc::VirtualDisk beta{L"beta",L"Docker",L"Docker",L"C:\\example\\docker.vhdx",24*GiB,true};
    app.snapshot.disks={missing,alpha,beta}; RefreshDiskChoices();
    SmokeRequire(SendMessageW(Control(Disks),CB_GETCOUNT,0,0)==2 && SelectedDisk()==L"alpha","measured choices and identity mapping");
    SmokeRequire(Text(DiskPath)==alpha.path && CopyPathText()==alpha.path && Text(Budget)==FormatGiB(GiB+1),"selected exact bytes and full copy path");
    Set(Budget,L"2.5"); Set(Reserve,L"29.5"); Check(Dangling,true);
    app.has_plan=true; app.plan.target=L"reviewed-target"; app.plan.token=L"reviewed-token";
    app.plan_text=L"ORIGINAL REVIEWED PLAN"; Set(PlanView,app.plan_text); Set(Consent,L"CLEAN");
    app.snapshot.disks={beta,missing}; RefreshDiskChoices(); Render();
    SmokeRequire(app.budget_dirty && app.selected_id==L"alpha" && SelectedDisk()==L"alpha" && Text(Budget)==L"2.5" &&
        Text(Reserve)==L"29.5" && Checked(Dangling) && Text(DiskPath)==alpha.path && Text(DiskState).find(L"indisponível")!=std::wstring::npos,"unavailable target preserves unsaved controls");
    SmokeRequire(app.has_plan && app.plan.target==L"reviewed-target" && app.plan.token==L"reviewed-token" &&
        Text(PlanView)==L"ORIGINAL REVIEWED PLAN" && Text(Consent)==L"CLEAN","refresh preserves reviewed plan and consent");
    SmokeRequire(Text(Report).find(missing.path)!=std::wstring::npos && app.config.budgets.at(L"unseen")==77,"diagnostics retains candidates and unseen budgets");
    SelectIdentity(L"beta"); ChangeDiskSelection(DiscardDecision::Keep);
    SmokeRequire(app.selected_id==L"alpha" && Text(Budget)==L"2.5" && app.has_plan,"refused discard preserves edit and plan");
    app.snapshot.disks={missing,beta,alpha}; RefreshDiskChoices();
    SmokeRequire(SelectedDisk()==L"alpha" && Text(Budget)==L"2.5" && app.has_plan,"reordered returning disk maps by identity");
    Check(Shutdown,true);
    SelectIdentity(L"beta"); ChangeDiskSelection(DiscardDecision::Discard);
    SmokeRequire(app.selected_id==L"beta" && Text(Budget)==L"0" && !app.budget_dirty && !app.has_plan,"confirmed switch reloads only new budget and invalidates plan");
    SmokeRequire(Text(Consent).empty() && !Checked(Shutdown),"confirmed switch clears consent and shutdown approval");
    app.has_plan=true; app.busy=true; EnableActions();
    for(int id:{Disks,Budget,Reserve,SaveDisk,SaveGlobal,Docker,Apt,Compact,Execute,Consent,Shutdown})
        SmokeRequire(!IsWindowEnabled(Control(id)),"busy disables mutation and maintenance");
    SmokeRequire(IsWindowEnabled(Control(Exit)),"exit remains available while busy");
    app.busy=false; app.has_plan=false; EnableActions();
    // Unlike direct Keep/Discard checks above, these enter the Ask path and inject
    // RefreshDiskChoices inside the prompt, after capture of beta. Run both cases
    // before reporting failures, and restore the preceding stable fixture first.
    const auto statusBeforeReentry=app.status;
    auto resetReentryFixture=[&]() {
        app.config=current; app.snapshot.disks={missing,beta,alpha};
        app.selected_id=L"beta"; app.selected_disk=beta;
        RefreshDiskChoices(); LoadSelectedBudget(); app.plan=wc::Plan{};
        InvalidatePlan(); app.status=statusBeforeReentry; Render(); EnableActions();
    };
    int promptCalls=0;
    bool reorderedResult=false,disappearedResult=false;
    auto reentrantSwitch=[&](bool disappear) {
        app.config=current; app.snapshot.disks={missing,alpha,beta};
        app.selected_id=L"alpha"; app.selected_disk=alpha;
        RefreshDiskChoices(); LoadSelectedBudget(); Set(Budget,L"2.5");
        app.has_plan=true; app.plan.target=L"reviewed-target"; app.plan.token=L"reviewed-token";
        app.plan_text=L"ORIGINAL REVIEWED PLAN"; Set(PlanView,app.plan_text);
        Set(Consent,L"CLEAN"); Check(Shutdown,true);
        SelectIdentity(L"beta");
        bool enteredWithBeta=false,refreshedToAlpha=false;
        ChangeDiskSelection(DiscardDecision::Ask,[&]() {
            ++promptCalls;
            enteredWithBeta=SelectedDisk()==L"beta";
            app.snapshot.disks=disappear?std::vector<wc::VirtualDisk>{missing,alpha}:
                std::vector<wc::VirtualDisk>{beta,missing,alpha};
            RefreshDiskChoices();
            refreshedToAlpha=SelectedDisk()==L"alpha" && app.selected_id==L"alpha" &&
                Text(Budget)==L"2.5" && app.budget_dirty && app.has_plan;
            return DiscardDecision::Discard;
        });
        const bool approvalsCleared=Text(Consent).empty() && !Checked(Shutdown) && !app.has_plan;
        const bool unchangedConfig=SameGlobals(app.config,current) && app.config.budgets==current.budgets;
        if(!disappear) return enteredWithBeta && refreshedToAlpha && unchangedConfig &&
            app.selected_id==L"beta" && SelectedDisk()==L"beta" && Text(DiskPath)==beta.path &&
            CopyPathText()==beta.path && Text(Budget)==L"0" && !app.budget_dirty && approvalsCleared;
        // A vanished prospective target may safely cancel the switch, or be retained
        // explicitly as unavailable. Neither policy may silently show alpha while
        // saving beta, discard alpha's edit without changing target, or copy alpha's path for beta.
        const bool keptAlpha=app.selected_id==L"alpha" && SelectedDisk()==L"alpha" &&
            Text(DiskPath)==alpha.path && CopyPathText()==alpha.path && Text(Budget)==L"2.5" &&
            app.budget_dirty && app.has_plan && app.plan.target==L"reviewed-target" &&
            app.plan.token==L"reviewed-token" && Text(PlanView)==L"ORIGINAL REVIEWED PLAN" &&
            Text(Consent)==L"CLEAN" && Checked(Shutdown);
        const bool retainedBeta=app.selected_id==L"beta" && SelectedDisk()==L"beta" &&
            SendMessageW(Control(Disks),CB_GETCURSEL,0,0)==CB_ERR && Text(DiskPath)==beta.path &&
            CopyPathText()==beta.path && Text(DiskState).find(L"indisponível")!=std::wstring::npos &&
            Text(Budget)==L"0" && !app.budget_dirty && approvalsCleared;
        return enteredWithBeta && refreshedToAlpha && unchangedConfig && (keptAlpha || retainedBeta);
    };
    try {
        reorderedResult=reentrantSwitch(false);
        disappearedResult=reentrantSwitch(true);
    } catch(...) { resetReentryFixture(); throw; }
    resetReentryFixture();
    SmokeRequire(promptCalls==2,"reentrant selection prompt seam exercised twice");
    SmokeRequire(reorderedResult,"reentrant selection reordered target keeps combo path and save identity aligned");
    SmokeRequire(disappearedResult,"reentrant selection disappeared target preserves a coherent available or unavailable identity");
    SmokeRequire(!app.smoke_failed && !app.worker.joinable() && !app.pending && app.config_path.empty() && !app.tray,"smoke no resident initialization");
}
void ExitApp() {
    app.closing=true;
    if(!app.busy) KillTimer(app.window,PollTimer);
    EnableActions();
    if(app.busy) { app.status=L"Saída solicitada: aguardando a operação ativa com segurança. Não interrompa durante manutenção."; Render(); Show(); }
    else DestroyWindow(app.window);
}
LRESULT WindowProcImpl(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CLOSE: if(app.tray) ShowWindow(window,SW_HIDE); else Show(); return 0;
    case WM_GETMINMAXINFO: {
        RECT r{0,0,760,720};
        AdjustWindowRectEx(&r,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,0);
        auto info=reinterpret_cast<MINMAXINFO*>(lp); info->ptMinTrackSize={r.right-r.left,r.bottom-r.top}; return 0;
    }
    case WM_SIZE: Layout(LOWORD(lp),HIWORD(lp)); return 0;
    case WM_NOTIFY: {
        auto item=reinterpret_cast<NMHDR*>(lp);
        if(item->idFrom==Tabs && item->code==TCN_SELCHANGE) SetPage(TabCtrl_GetCurSel(Control(Tabs)));
        return 0;
    }
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
            auto menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,1,L"Mostrar painel"); AppendMenuW(menu,MF_STRING,2,L"Sair (aguarda manutenção)");
            POINT p; GetCursorPos(&p); SetForegroundWindow(window);
            auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr); DestroyMenu(menu);
            if(choice==1) Show(); else if(choice==2) ExitApp();
        }
        return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==Budget && HIWORD(wp)==EN_CHANGE && !app.setting_fields) {
            app.budget_dirty=Text(Budget)!=app.budget_display; RenderDisk(); return 0;
        }
        if(LOWORD(wp)==CopyPath) { CopyDiskPath(); return 0; }
        if(app.busy || app.closing) { if(LOWORD(wp)==Exit) ExitApp(); return 0; }
        if(LOWORD(wp)==Disks && HIWORD(wp)==CBN_SELCHANGE) { ChangeDiskSelection(); return 0; }
        switch(LOWORD(wp)) {
        case SaveDisk:SaveDiskBudget();break;
        case SaveGlobal:SaveGlobalSettings();break;
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
            app.snapshot=std::move(w->snapshot);
            RefreshDiskChoices();
            if(!w->message.empty()) app.status=w->message;
            Render();
            if(!w->alerts.empty()) {
                std::wstring message;
                for(const auto& a:w->alerts) message+=a.message+L"\r\n";
                app.status+=L"\r\nALERTA: "+message; Render();
                NOTIFYICONDATAW ni{}; ni.cbSize=sizeof(ni); ni.hWnd=window; ni.uID=1; ni.uFlags=NIF_INFO;
                lstrcpynW(ni.szInfoTitle,L"WSL Cleaner — alerta de disco",64); lstrcpynW(ni.szInfo,message.c_str(),256); ni.dwInfoFlags=NIIF_WARNING;
                Shell_NotifyIconW(NIM_MODIFY,&ni);
                Show(); // Also visible when notifications are suppressed by Windows.
            }
        } else if(w->kind==Work::Prepared) {
            app.plan=std::move(w->plan); app.has_plan=true;
            std::wstring full=L"PLANO PARA REVISÃO — uso único; validade de cinco minutos.\r\nExpira (segundos Unix): "+std::to_wstring(app.plan.expires)+
                L"\r\nAlvo exato: "+app.plan.target+L"\r\nPasta da cópia de segurança: "+app.plan.backup_directory+L"\r\n";
            if(app.plan.action==wc::Action::Compact)
                full+=L"COMPACTAÇÃO: TODAS as distribuições WSL e cargas Docker serão paradas. Exige administrador, VHDX fora de uso, acesso exclusivo e cópia integral consistente verificada em OUTRO volume fixo. Não reinicie nem interrompa a máquina. Sem garantia de redução mínima. Digite COMPACT e autorize a parada separadamente.\r\n";
            else if(app.plan.action==wc::Action::WslAptCache)
                full+=L"LIMPEZA WSL: apt-get clean pode iniciar a distribuição nomeada como root. Remove arquivos de pacotes baixados, não aplicativos instalados nem arquivos pessoais. Exclusão irreversível, possivelmente parcial. Digite CLEAN.\r\n";
            else full+=L"LIMPEZA DOCKER: serviço local inteiro, independente do disco selecionado. Somente categorias e idade salvas. Exclusão irreversível, possivelmente parcial; volumes e contêineres comuns excluídos. Digite CLEAN.\r\n";
            full+=L"\r\nETAPAS TÉCNICAS ORIGINAIS (podem estar em inglês; preservadas):\r\n";
            for(const auto& step:app.plan.steps) full+=step+L"\r\n";
            app.plan_text=Lines(full); Set(PlanView,app.plan_text); SetPage(MaintenancePage); Show();
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
        if(app.smoke) { app.smoke_failed=true; return 0; }
        MessageBoxW(window,L"Falha na interface. Este evento não iniciará nova manutenção. Use Sair para aguardar com segurança qualquer operação ativa.",L"WSL Cleaner",MB_OK|MB_ICONERROR);
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
        if(GetLastError()==ERROR_ALREADY_EXISTS) { CloseHandle(mutex); MessageBoxW(nullptr,L"WSL Cleaner já está em execução para este usuário. Abra pelo ícone da bandeja.",L"WSL Cleaner",MB_OK); return 0; }
    }
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int code=0;
    try {
        if(FAILED(com)) throw std::runtime_error("COM initialization");
        if(!app.smoke) {
            app.config_path=wc::DataDirectory()+L"\\settings.conf";
            std::wstring error;
            app.valid=wc::LoadConfig(app.config_path,app.config,error);
            if(!app.valid) { app.config=wc::Config{}; app.status=L"ERRO DE CONFIGURAÇÃO: "+error+L" Automação desativada até salvar configurações válidas explicitamente. O arquivo existente não será sobrescrito automaticamente."; }
            else app.status=L"Monitorando. Manutenção usa somente configurações salvas e categorias autorizadas.";
        }
        // Only the tab class comes from comctl32. EDIT/BUTTON/STATIC/COMBOBOX
        // are USER32 standard classes, not groups we need this DLL to register.
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES};
        if(!InitCommonControlsEx(&controls)) throw std::runtime_error("common controls");
        WNDCLASSW klass{}; klass.lpfnWndProc=WindowProc; klass.hInstance=instance; klass.lpszClassName=L"WslCleanerNative";
        klass.hIcon=LoadIconW(nullptr,IDI_APPLICATION); klass.hCursor=LoadCursorW(nullptr,IDC_ARROW); klass.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        if(!RegisterClassW(&klass)) throw std::runtime_error("register window");
        app.window=CreateWindowExW(0,klass.lpszClassName,L"WSL Cleaner — monitor de espaço",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_THICKFRAME|WS_CLIPCHILDREN,
            CW_USEDEFAULT,CW_USEDEFAULT,1000,880,nullptr,nullptr,instance,nullptr);
        if(!app.window) throw std::runtime_error("create window");
        Controls();
        if(app.smoke) { HiddenPanelChecks(); Print(L"SMOKE OK: native controls initialized; no tray, polling or maintenance.\r\n"); DestroyWindow(app.window); }
        else {
            NOTIFYICONDATAW ni{}; ni.cbSize=sizeof(ni); ni.hWnd=app.window; ni.uID=1;
            ni.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; ni.uCallbackMessage=Tray; ni.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
            lstrcpynW(ni.szTip,L"WSL Cleaner — monitor de espaço",128);
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
    } catch(const std::exception& error) {
        if(app.smoke) { const std::string detail=error.what(); Print(L"SMOKE FAILED: "+std::wstring(detail.begin(),detail.end())+L"\r\n"); }
        else MessageBoxW(nullptr,L"Falha ao inicializar o aplicativo nativo.",L"WSL Cleaner",MB_OK|MB_ICONERROR);
        code=1;
    } catch(...) {
        if(app.smoke) Print(L"SMOKE FAILED: native panel initialization.\r\n");
        else MessageBoxW(nullptr,L"Falha ao inicializar o aplicativo nativo.",L"WSL Cleaner",MB_OK|MB_ICONERROR);
        code=1;
    }
    // Also protects initialization/message-loop exception paths. No worker is detached.
    if(app.worker.joinable()) app.worker.join();
    if(IsWindow(app.window)) {
        DestroyWindow(app.window);
    }
    if(app.font) DeleteObject(app.font); if(app.black) DeleteObject(app.black);
    if(SUCCEEDED(com)) CoUninitialize(); if(mutex) CloseHandle(mutex);
    return code;
}
