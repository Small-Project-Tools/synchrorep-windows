#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <thread>
#include <map>
#include <optional>
#include <vector>
#include "async_job.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include "synchrorep/recovery_catalog.hpp"
#include <wincred.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "synchrorep/engine.hpp"
#include "synchrorep/profiles.hpp"
#include "synchrorep/remote_endpoint.hpp"
#include "synchrorep/sha256.hpp"
#include "synchrorep/mirror_monitor.hpp"
#include "startup_registry.hpp"
#include "synchrorep/startup_profiles.hpp"

namespace {
using namespace synchrorep;
enum Control : int {
 IdProfiles=101,IdNew=102,IdSave=103,IdDelete=104,IdName=105,
 IdLeft=106,IdRight=107,IdLeftUser=108,IdLeftPassword=109,
 IdRightUser=110,IdRightPassword=111,IdKnownHosts=112,
 IdRemember=113,IdLoadCredentials=114,IdPolicy=115,IdDeletes=116,
 IdPreview=117,IdExecute=118,IdChanges=119,IdExplorer=120,
 IdCancel=121,IdKeepLeft=122,IdKeepRight=123,IdSkip=124,IdProgress=125,IdStatus=126,IdResume=127,
 IdProtect=128,IdOpenRecovery=129,IdOpenLogs=130,IdRecycle=131,IdMirror=132,IdAutoStart=133
};
HWND controls[140]{};
HWND mainWindow=nullptr;
HFONT font=nullptr;
std::vector<Profile> profiles;
int selectedProfile=-1;
bool loading=false;
std::unique_ptr<synchrorep::winui::AsyncJob> job;
bool busy=false;
std::map<std::string,ConflictChoice> overrides;
std::vector<Decision> shownDecisions;
HWND recoveryWindow=nullptr;
HWND recoveryList=nullptr;
HWND recoveryInfo=nullptr;
std::vector<RecoveryItem> recoveryRecords;
constexpr UINT TrayMessage=WM_APP+100;
constexpr UINT TrayId=5;
bool trayAdded=false;
bool activeMirror=false;
HANDLE mirrorMutex=nullptr;
HANDLE legacyMirrorMutex=nullptr;
void releaseMirrorLock(){
 if(legacyMirrorMutex){CloseHandle(legacyMirrorMutex);legacyMirrorMutex=nullptr;}
 if(mirrorMutex){CloseHandle(mirrorMutex);mirrorMutex=nullptr;}
}
void acquireMirrorLock(const Profile& profile){
 if(mirrorMutex)throw std::runtime_error("A mirror already runs in this window");
 const auto key=mirror_target_mutex(profile.right);
 const auto lockName=std::wstring(key.begin(),key.end());
 HANDLE handle=CreateMutexW(nullptr,FALSE,lockName.c_str());
 if(!handle)throw std::runtime_error("Cannot create mirror destination lock");
 if(GetLastError()==ERROR_ALREADY_EXISTS){
  CloseHandle(handle);
  throw std::runtime_error("Another continuous mirror already uses this destination");
 }
 // During upgrades, also retain the 0.12.1 pair-based lock. This prevents
 // the same saved profile from running concurrently across old/new binaries.
 const auto previous=profile.left+"\n"+profile.right;
 Sha256 hash;
 hash.update(reinterpret_cast<const unsigned char*>(previous.data()),previous.size());
 const auto previousHash=hash.finish();
 const auto legacyName=L"Local\\SynchrorepMirror-"+std::wstring(previousHash.begin(),previousHash.end());
 HANDLE legacy=CreateMutexW(nullptr,FALSE,legacyName.c_str());
 if(!legacy){
  CloseHandle(handle);
  throw std::runtime_error("Cannot create backward-compatible mirror instance lock");
 }
 if(GetLastError()==ERROR_ALREADY_EXISTS){
  CloseHandle(legacy);
  CloseHandle(handle);
  throw std::runtime_error("This mirror is already running in another Synchrorep version");
 }
 mirrorMutex=handle;
 legacyMirrorMutex=legacy;
}
void showMainWindow(){
 ShowWindow(mainWindow,SW_RESTORE);
 ShowWindow(mainWindow,SW_SHOW);
 SetForegroundWindow(mainWindow);
}
void removeTray(){
 if(!trayAdded)return;
 NOTIFYICONDATAW info{};
 info.cbSize=sizeof(info);
 info.hWnd=mainWindow;
 info.uID=TrayId;
 Shell_NotifyIconW(NIM_DELETE,&info);
 trayAdded=false;
}
void addTray(){
 if(trayAdded)return;
 NOTIFYICONDATAW info{};
 info.cbSize=sizeof(info);
 info.hWnd=mainWindow;
 info.uID=TrayId;
 info.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
 info.uCallbackMessage=TrayMessage;
 info.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));
 wcscpy_s(info.szTip,L"Synchrorep - Mirror monitoring active");
 trayAdded=Shell_NotifyIconW(NIM_ADD,&info)!=FALSE;
}


struct JobSettings {
 Profile profile;
 std::string leftUser,leftPassword,rightUser,rightPassword;
 std::map<std::string,ConflictChoice> overrides;
 bool execute=false;
 bool resume=false;
};


std::wstring toWide(const std::string& s){
 if(s.empty())return {};
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
 if(!n)throw std::runtime_error("Invalid UTF-8 string");
 std::wstring result(static_cast<std::size_t>(n),L'\0');
 MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),result.data(),n);
 return result;
}
std::string toUtf8(const std::wstring& s){
 if(s.empty())return {};
 int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
 if(!n)throw std::runtime_error("Invalid Windows Unicode input");
 std::string result(static_cast<std::size_t>(n),'\0');
 WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),result.data(),n,nullptr,nullptr);
 return result;
}
std::wstring get(int id){
 HWND h=controls[id];
 int length=GetWindowTextLengthW(h);
 std::wstring result(static_cast<std::size_t>(length+1),L'\0');
 GetWindowTextW(h,result.data(),length+1);
 result.resize(length);
 return result;
}
void set(int id,const std::wstring& text){SetWindowTextW(controls[id],text.c_str());}
std::filesystem::path appDirectory(){
 wchar_t local[32768]{};
 const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
 if(length==0||length>=32768)throw std::runtime_error("Cannot access LOCALAPPDATA");
 return std::filesystem::path(local)/L"Synchrorep";
}
std::filesystem::path statePath(const Endpoint& left,const Endpoint& right){
 auto key=left.identity()+"\n"+right.identity();
 Sha256 hash;hash.update(reinterpret_cast<const unsigned char*>(key.data()),key.size());
 return appDirectory()/(toWide(hash.finish())+L".state");
}
std::wstring credentialTarget(const std::string& uri){
 if(endpoint_kind(uri)!=EndpointKind::Ftp && endpoint_kind(uri)!=EndpointKind::Sftp)return {};
 auto sep=uri.find("://");
 auto authority=uri.substr(0,uri.find('/',sep+3));
 if(authority.find('@')!=std::string::npos)throw std::runtime_error("Credentials must not appear inside a remote URI");
 return L"Synchrorep:"+toWide(authority);
}
HWND place(HWND parent,const wchar_t* type,const wchar_t* value,DWORD style,
           int x,int y,int w,int h,int id=0,DWORD extra=0){
 auto win=CreateWindowExW(extra,type,value,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr),nullptr);
 if(!win)throw std::runtime_error("Could not create Windows control");
 if(font)SendMessageW(win,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
 if(id)controls[id]=win;
 return win;
}
void label(HWND h,const wchar_t* text,int x,int y,int w=350){
 place(h,L"STATIC",text,0,x,y,w,20);
}
void edit(HWND h,int id,int x,int y,int w,bool password=false){
 place(h,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|(password?ES_PASSWORD:0),
       x,y,w,25,id,WS_EX_CLIENTEDGE);
}
void button(HWND h,const wchar_t* text,int id,int x,int y,int w){
 place(h,L"BUTTON",text,WS_TABSTOP,x,y,w,30,id);
}
std::wstring operationName(Action a){
 switch(a){
 case Action::CopyLeftToRight:return L"Left -> Right";
 case Action::CopyRightToLeft:return L"Right -> Left";
 case Action::DeleteLeft:return L"Delete left";
 case Action::DeleteRight:return L"Delete right";
 case Action::Conflict:return L"CONFLICT";
 case Action::None:return L"No change";
 case Action::Skip:return L"Skipped";
 }
 return L"Unknown";
}
void updateList(){
 loading=true;
 SendMessageW(controls[IdProfiles],LB_RESETCONTENT,0,0);
 for(const auto& p:profiles){
  auto name=toWide(p.name);
  SendMessageW(controls[IdProfiles],LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
 }
 if(selectedProfile>=0&&selectedProfile<static_cast<int>(profiles.size()))
  SendMessageW(controls[IdProfiles],LB_SETCURSEL,selectedProfile,0);
 loading=false;
}
void clearCredentials(){
 set(IdLeftUser,L"");set(IdLeftPassword,L"");
 set(IdRightUser,L"");set(IdRightPassword,L"");
}
void loadProfile(int index){
 if(index<0||index>=static_cast<int>(profiles.size()))return;
 selectedProfile=index;overrides.clear();shownDecisions.clear();const auto& p=profiles.at(static_cast<std::size_t>(index));
 set(IdName,toWide(p.name));set(IdLeft,toWide(p.left));set(IdRight,toWide(p.right));
 set(IdKnownHosts,toWide(p.known_hosts));
 SendMessageW(controls[IdPolicy],CB_SETCURSEL,static_cast<WPARAM>(p.conflict),0);
 SendMessageW(controls[IdDeletes],BM_SETCHECK,p.propagate_deletions?BST_CHECKED:BST_UNCHECKED,0);
 SendMessageW(controls[IdProtect],BM_SETCHECK,p.preserve_changes?BST_CHECKED:BST_UNCHECKED,0);
 SendMessageW(controls[IdRecycle],BM_SETCHECK,p.recycle_local_deletions?BST_CHECKED:BST_UNCHECKED,0);
 SendMessageW(controls[IdMirror],BM_SETCHECK,p.mode==SyncMode::Mirror?BST_CHECKED:BST_UNCHECKED,0);
 SendMessageW(controls[IdAutoStart],BM_SETCHECK,p.auto_start?BST_CHECKED:BST_UNCHECKED,0);
 EnableWindow(controls[IdAutoStart],p.mode==SyncMode::Mirror);
 SendMessageW(controls[IdChanges],LB_RESETCONTENT,0,0);
 clearCredentials();updateList();
}
Profile fromForm(){
 Profile p; p.name=toUtf8(get(IdName));
 p.left=toUtf8(get(IdLeft));p.right=toUtf8(get(IdRight));
 p.known_hosts=toUtf8(get(IdKnownHosts));
 const auto idx=SendMessageW(controls[IdPolicy],CB_GETCURSEL,0,0);
 p.conflict=idx>=0&&idx<=4?static_cast<ConflictPolicy>(idx):ConflictPolicy::Ask;
 p.propagate_deletions=SendMessageW(controls[IdDeletes],BM_GETCHECK,0,0)==BST_CHECKED;
 p.preserve_changes=SendMessageW(controls[IdProtect],BM_GETCHECK,0,0)==BST_CHECKED;
 p.recycle_local_deletions=SendMessageW(controls[IdRecycle],BM_GETCHECK,0,0)==BST_CHECKED;
 p.mode=SendMessageW(controls[IdMirror],BM_GETCHECK,0,0)==BST_CHECKED
   ?SyncMode::Mirror:SyncMode::Bidirectional;
 p.auto_start=p.mode==SyncMode::Mirror&&
   SendMessageW(controls[IdAutoStart],BM_GETCHECK,0,0)==BST_CHECKED;
 return p;
}
std::wstring credentialTargetForm(int uriId){return credentialTarget(toUtf8(get(uriId)));}
bool storeCredential(int uriId,int usernameId,int passwordId){
 auto target=credentialTargetForm(uriId);
 if(target.empty())return false;
 auto username=get(usernameId),password=get(passwordId);
 if(username.empty()||password.empty())return false;
 CREDENTIALW record{};
 record.Type=CRED_TYPE_GENERIC;
 record.TargetName=const_cast<LPWSTR>(target.c_str());
 record.UserName=const_cast<LPWSTR>(username.c_str());
 record.CredentialBlobSize=static_cast<DWORD>(password.size()*sizeof(wchar_t));
 record.CredentialBlob=reinterpret_cast<LPBYTE>(password.data());
 record.Persist=CRED_PERSIST_LOCAL_MACHINE;
 if(!CredWriteW(&record,0))throw std::runtime_error("Failed storing credentials in Windows Credential Manager");
 return true;
}
bool loadCredential(int uriId,int usernameId,int passwordId){
 auto target=credentialTargetForm(uriId);
 if(target.empty())return false;
 PCREDENTIALW record=nullptr;
 if(!CredReadW(target.c_str(),CRED_TYPE_GENERIC,0,&record))return false;
 std::unique_ptr<CREDENTIALW,decltype(&CredFree)> owned(record,CredFree);
 if(record->UserName)set(usernameId,record->UserName);
 if(record->CredentialBlob && record->CredentialBlobSize%sizeof(wchar_t)==0){
  auto begin=reinterpret_cast<const wchar_t*>(record->CredentialBlob);
  set(passwordId,std::wstring(begin,begin+record->CredentialBlobSize/sizeof(wchar_t)));
 }
 return true;
}
std::unique_ptr<Endpoint> endpointFrom(const std::string& uri,
  const std::string& username,const std::string& password,const std::string& known_hosts) {
 auto kind=endpoint_kind(uri);
 if(kind!=EndpointKind::Ftp && kind!=EndpointKind::Sftp)return open_endpoint(uri);
#ifndef SYNCHROREP_REMOTE_ENABLED
 throw std::runtime_error("FTP/SFTP are not available in the Lite build. Install the full MSVC/vcpkg edition.");
#endif
 RemoteCredentials credentials{username,password,known_hosts,{}};
 if(credentials.username.empty())throw std::runtime_error("Remote username is required");
 return std::make_unique<RemoteEndpoint>(uri,std::move(credentials));
}
void showPlan(const std::vector<Decision>& plan){
 shownDecisions.clear();
 SendMessageW(controls[IdChanges],LB_RESETCONTENT,0,0);
 for(const auto& d:plan){
  if(d.action==Action::None)continue;
  shownDecisions.push_back(d);
  auto text=operationName(d.action)+L" : "+toWide(d.relative);
  SendMessageW(controls[IdChanges],LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
 }
}
void setBusy(bool running){
 busy=running;
 for(int id:{IdProfiles,IdNew,IdSave,IdDelete,IdName,IdLeft,IdRight,
  IdLeftUser,IdLeftPassword,IdRightUser,IdRightPassword,IdKnownHosts,
  IdRemember,IdLoadCredentials,IdPolicy,IdDeletes,IdPreview,IdExecute,
  IdExplorer,IdKeepLeft,IdKeepRight,IdSkip,IdResume,IdProtect,IdOpenRecovery,IdOpenLogs,IdRecycle,IdMirror,IdAutoStart})
   EnableWindow(controls[id],!running);
 EnableWindow(controls[IdCancel],running);
}
JobSettings getJobSettings(bool execute){
 JobSettings settings;
 settings.profile=fromForm();
 if(settings.profile.left.empty()||settings.profile.right.empty())
  throw std::runtime_error("Both endpoints must be configured");
 if(settings.profile.mode==SyncMode::Mirror){
  if(endpoint_kind(settings.profile.left)!=EndpointKind::Local)
   throw std::runtime_error("Mirror source must be a local Windows folder, not FTP/SFTP/WSL");
#ifdef _WIN32
  const auto directory=std::filesystem::path(toWide(settings.profile.left));
  const auto native=std::filesystem::absolute(directory).native();
  if(native.size()<3||native[1]!=L':'||
     (native[2]!=L'\\'&&native[2]!=L'/'))
   throw std::runtime_error("Mirror source must be a local drive, not a network share");
#endif
 }
 settings.leftUser=toUtf8(get(IdLeftUser));
 settings.leftPassword=toUtf8(get(IdLeftPassword));
 settings.rightUser=toUtf8(get(IdRightUser));
 settings.rightPassword=toUtf8(get(IdRightPassword));
 settings.overrides=overrides;
 settings.execute=execute;
 settings.resume=SendMessageW(controls[IdResume],BM_GETCHECK,0,0)==BST_CHECKED;
 return settings;
}
void startSync(bool execute,bool trustedStartup=false){
 if(busy)return;
 auto settings=getJobSettings(execute);
 if(execute&&!trustedStartup&&MessageBoxW(mainWindow,
   settings.profile.mode==SyncMode::Mirror
    ?L"MIRROR MODE: the local folder is authoritative. Extra target files are deleted and target edits overwritten on each reconciliation. Backups are enabled by default. Start continuous monitoring?"
    :L"Local fixed-drive deletions can use the Windows Recycle Bin. WSL/UNC and FTP/SFTP may permanently delete originals after recovery archiving (if enabled). Review Preview first. Continue?",
   L"Confirm synchronization",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
 if(!job)job=std::make_unique<synchrorep::winui::AsyncJob>(mainWindow);
 SendMessageW(controls[IdProgress],PBM_SETPOS,0,0);
 set(IdStatus,L"Starting...");
 setBusy(true);
 const bool mirrorMode=settings.profile.mode==SyncMode::Mirror&&execute;
 try{
  if(mirrorMode){
   for(const auto& other:profiles){
    if(other.mode!=SyncMode::Mirror||other.name==settings.profile.name)continue;
    if(mirror_targets_overlap(settings.profile.right,other.right))
     throw std::runtime_error("Another configured mirror uses this destination or a parent/subfolder");
   }
   acquireMirrorLock(settings.profile);
  }
  activeMirror=mirrorMode;
  if(activeMirror)addTray();
  job->start([settings=std::move(settings)](const RunCallbacks& callbacks){
   synchrorep::winui::FinishedEvent result;
   auto a=endpointFrom(settings.profile.left,settings.leftUser,
                       settings.leftPassword,settings.profile.known_hosts);
   auto b=endpointFrom(settings.profile.right,settings.rightUser,
                       settings.rightPassword,settings.profile.known_hosts);
   Options options{settings.profile.conflict,settings.profile.propagate_deletions,
                   settings.overrides,settings.resume,settings.profile.preserve_changes,
                    settings.profile.recycle_local_deletions,settings.profile.mode};
   Engine engine(statePath(*a,*b),std::move(options));
   result.decisions=engine.plan(*a,*b,callbacks);
   if(settings.execute){
    const bool conflicts=std::any_of(result.decisions.begin(),result.decisions.end(),
      [](const Decision& decision){return decision.action==Action::Conflict;});
    if(conflicts&&(settings.profile.mode==SyncMode::Mirror||
                   settings.profile.conflict!=ConflictPolicy::Ignore)){
     result.error="Resolve each conflict using the Keep left, Keep right, or Skip buttons, then retry.";
    }else{
     if(settings.profile.mode==SyncMode::Mirror){
      auto* local=dynamic_cast<LocalEndpoint*>(a.get());
      if(!local)throw std::runtime_error("Mirror source is not a local endpoint");
      MirrorMonitor monitor;
      result.result=monitor.run(*a,*b,engine,local->root(),callbacks);
     }else result.result=engine.run(*a,*b,false,callbacks);
     result.executed=true;
    }
   }
   return result;
  });
 }catch(...){
  activeMirror=false;
  removeTray();
  releaseMirrorLock();
  setBusy(false);
  throw;
 }
}
void chooseConflict(ConflictChoice choice){
 if(busy)return;
 const auto selected=SendMessageW(controls[IdChanges],LB_GETCURSEL,0,0);
 if(selected<0||static_cast<std::size_t>(selected)>=shownDecisions.size()){
  MessageBoxW(mainWindow,L"Select a conflicting file first.",L"Synchrorep",MB_ICONINFORMATION);
  return;
 }
 const auto& d=shownDecisions.at(static_cast<std::size_t>(selected));
 if(d.action!=Action::Conflict&&d.action!=Action::Skip&&!overrides.count(d.relative)){
  MessageBoxW(mainWindow,L"The selected file is not a conflict.",L"Synchrorep",MB_ICONINFORMATION);
  return;
 }
 overrides[d.relative]=choice;
 auto verb=choice==ConflictChoice::KeepLeft?L"Keep LEFT":
  choice==ConflictChoice::KeepRight?L"Keep RIGHT":L"Skip";
 std::wstring text=std::wstring(verb)+L" : "+toWide(d.relative);
 SendMessageW(controls[IdChanges],LB_DELETESTRING,selected,0);
 SendMessageW(controls[IdChanges],LB_INSERTSTRING,selected,reinterpret_cast<LPARAM>(text.c_str()));
 SendMessageW(controls[IdChanges],LB_SETCURSEL,selected,0);
 set(IdStatus,L"Conflict choice saved for this run. Preview again to validate it.");
}
std::wstring phaseName(Phase phase){
 switch(phase){
 case Phase::ScanLeft:return L"Scan left";
 case Phase::ScanRight:return L"Scan right";
 case Phase::Copy:return L"Copy";
 case Phase::Delete:return L"Delete";
 case Phase::VerifyLeft:return L"Verify left";
 case Phase::VerifyRight:return L"Verify right";
 case Phase::Journal:return L"Write recovery journal";
 case Phase::Save:return L"Save history";
 case Phase::Complete:return L"Complete";
 case Phase::Watch:return L"Mirror active - watching source";
 }
 return L"Processing";
}
void onProgress(std::unique_ptr<synchrorep::winui::ProgressEvent> event){
 if(!busy)return;
 const auto& info=event->state;
 const auto range=static_cast<LPARAM>(std::min<std::size_t>(info.total,1000000));
 SendMessageW(controls[IdProgress],PBM_SETRANGE32,0,std::max<LPARAM>(1,range));
 SendMessageW(controls[IdProgress],PBM_SETPOS,static_cast<WPARAM>(std::min<std::size_t>(info.completed,1000000)),0);
 auto text=phaseName(info.phase)+L" "+
  std::to_wstring(info.completed)+L"/"+std::to_wstring(info.total);
 if(!info.relative.empty())text+=L" : "+toWide(info.relative);
 set(IdStatus,text);
}
void onFinished(std::unique_ptr<synchrorep::winui::FinishedEvent> result){
 if(job)job->join();
 const bool wasMirror=activeMirror;
 activeMirror=false;
 removeTray();
 releaseMirrorLock();
 if(wasMirror&&!IsWindowVisible(mainWindow))showMainWindow();
 setBusy(false);
 showPlan(result->decisions);
 if(result->cancelled){
  set(IdStatus,L"Cancelled between files; completed changes remain.");
  MessageBoxW(mainWindow,L"Cancelled. Completed transfers are preserved. The next run will reconcile them.",
              L"Synchrorep",MB_OK|MB_ICONWARNING);
 }else if(!result->error.empty()){
  set(IdStatus,L"Operation failed or conflicts unresolved.");
  MessageBoxA(mainWindow,result->error.c_str(),"Synchrorep",MB_OK|MB_ICONERROR);
 }else if(result->executed){
  auto text=L"Copied: "+std::to_wstring(result->result.copied)+
            L"\nDeleted: "+std::to_wstring(result->result.removed)+
            L"\nRecovery copies: "+std::to_wstring(result->result.archived)+
            L"\nRecycled: "+std::to_wstring(result->result.recycled)+
            L"\nConflicts: "+std::to_wstring(result->result.conflicts.size());
  set(IdStatus,wasMirror?L"Continuous mirror stopped.":L"Synchronization complete.");
  MessageBoxW(mainWindow,text.c_str(),L"Synchrorep",MB_OK|MB_ICONINFORMATION);
 }else set(IdStatus,L"Preview complete. Select a conflict to choose its resolution.");
}
enum RecoveryControl : int {
 RecoveryList=501,RecoveryRefresh=502,RecoveryExport=503,RecoveryFolder=504
};
void reloadRecovery(){
 auto archive=appDirectory()/"recovery";
 auto found=RecoveryCatalog::list(archive);
 recoveryRecords=std::move(found);
 SendMessageW(recoveryList,LB_RESETCONTENT,0,0);
 for(const auto& item:recoveryRecords){
  auto run=item.archive_side.parent_path().filename().wstring();
  auto display=L"["+run+L" / "+toWide(item.side)+L"] "+toWide(item.relative)+
   L"  ("+toWide(item.reason)+L")";
  SendMessageW(recoveryList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(display.c_str()));
 }
 auto info=std::to_wstring(recoveryRecords.size())+
   L" archived items. Select a file, then export to a chosen folder.";
 SetWindowTextW(recoveryInfo,info.c_str());
}
std::filesystem::path chooseRecoveryFolder(HWND owner){
 BROWSEINFOW config{};
 config.hwndOwner=owner;
 config.lpszTitle=L"Choose a folder for exported recovery files (existing files will not be overwritten)";
 config.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
 LPITEMIDLIST item=SHBrowseForFolderW(&config);
 if(!item)return {};
 wchar_t folder[MAX_PATH]{};
 auto success=SHGetPathFromIDListW(item,folder);
 CoTaskMemFree(item);
 if(!success)throw std::runtime_error("Could not resolve selected export folder");
 return folder;
}
void exportRecoveryItem(HWND window){
 auto selected=SendMessageW(recoveryList,LB_GETCURSEL,0,0);
 if(selected<0 || static_cast<std::size_t>(selected)>=recoveryRecords.size()){
  MessageBoxW(window,L"Select an archived item first.",L"Synchrorep recovery",MB_ICONINFORMATION);
  return;
 }
 const auto item=recoveryRecords[static_cast<std::size_t>(selected)];
 const auto destination=chooseRecoveryFolder(window);
 if(destination.empty())return;
 std::wstring summary=L"Export "+toWide(item.relative)+L" from "+toWide(item.side)+
  L" to "+destination.wstring()+
  L"?\n\nThe original synchronized locations will NOT be modified.";
 if(MessageBoxW(window,summary.c_str(),L"Confirm recovery export",
                MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
 const auto exported=RecoveryCatalog::export_item(item,destination);
 std::wstring done=L"Recovery exported to:\n"+exported.wstring()+
  L"\n\nInspect the file before manually restoring it to the original location.";
 MessageBoxW(window,done.c_str(),L"Synchrorep recovery",MB_OK|MB_ICONINFORMATION);
}
LRESULT CALLBACK recoveryProc(HWND window,UINT message,WPARAM w,LPARAM l){
 switch(message){
 case WM_CREATE:{
  CreateWindowW(L"STATIC",L"Archived versions (read-only; exports never overwrite files)",
    WS_CHILD|WS_VISIBLE,16,15,735,22,window,nullptr,GetModuleHandleW(nullptr),nullptr);
  recoveryList=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",
    WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY,
    16,43,750,318,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(RecoveryList)),
    GetModuleHandleW(nullptr),nullptr);
  recoveryInfo=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,16,372,745,28,
    window,nullptr,GetModuleHandleW(nullptr),nullptr);
  CreateWindowW(L"BUTTON",L"Refresh",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
    16,415,145,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(RecoveryRefresh)),
    GetModuleHandleW(nullptr),nullptr);
  CreateWindowW(L"BUTTON",L"Export selected",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
    173,415,185,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(RecoveryExport)),
    GetModuleHandleW(nullptr),nullptr);
  CreateWindowW(L"BUTTON",L"Open archive folder",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
    370,415,194,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(RecoveryFolder)),
    GetModuleHandleW(nullptr),nullptr);
  return 0;
 }
 case WM_COMMAND:{
  try {
   switch(LOWORD(w)){
   case RecoveryRefresh:reloadRecovery();break;
   case RecoveryExport:exportRecoveryItem(window);break;
   case RecoveryFolder:{
    auto folder=appDirectory()/"recovery";
    std::filesystem::create_directories(folder);
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",folder.c_str(),
        nullptr,nullptr,SW_SHOWNORMAL))<=32)
      throw std::runtime_error("Cannot open archive folder");
    break;
   }
   default:break;
   }
  }catch(const std::exception& e){
   MessageBoxA(window,e.what(),"Recovery error",MB_OK|MB_ICONERROR);
  }
  return 0;
 }
 case WM_DESTROY:recoveryWindow=nullptr;recoveryList=nullptr;recoveryInfo=nullptr;
  recoveryRecords.clear();return 0;
 default:return DefWindowProcW(window,message,w,l);
 }
}
void openRecoveryBrowser(){
 if(recoveryWindow && IsWindow(recoveryWindow)){
  ShowWindow(recoveryWindow,SW_SHOWNORMAL);
  SetForegroundWindow(recoveryWindow);return;
 }
 static bool registered=false;
 const auto module=GetModuleHandleW(nullptr);
 if(!registered){
  WNDCLASSW cls{};
  cls.lpfnWndProc=recoveryProc;
  cls.hInstance=module;
  cls.hIcon=LoadIconW(module,MAKEINTRESOURCEW(101));
  cls.hCursor=LoadCursor(nullptr,IDC_ARROW);
  cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
  cls.lpszClassName=L"SynchrorepRecovery";
  if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
   throw std::runtime_error("Could not register recovery window");
  registered=true;
 }
 recoveryWindow=CreateWindowW(L"SynchrorepRecovery",L"Synchrorep - Recover previous versions",
   WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
   CW_USEDEFAULT,CW_USEDEFAULT,800,505,mainWindow,nullptr,module,nullptr);
 if(!recoveryWindow)throw std::runtime_error("Could not create recovery window");
 try{reloadRecovery();}
 catch(...){DestroyWindow(recoveryWindow);throw;}
 ShowWindow(recoveryWindow,SW_SHOWNORMAL);
}
void setExplorerMenu(bool enabled){
 const wchar_t* key=L"Software\\Classes\\Directory\\shell\\Synchrorep";
 if(!enabled){
  auto code=RegDeleteTreeW(HKEY_CURRENT_USER,key);
  if(code!=ERROR_SUCCESS&&code!=ERROR_FILE_NOT_FOUND)throw std::runtime_error("Cannot remove Explorer menu");
  return;
 }
 HKEY out=nullptr;
 if(RegCreateKeyExW(HKEY_CURRENT_USER,key,0,nullptr,0,KEY_WRITE,nullptr,&out,nullptr)!=ERROR_SUCCESS)
  throw std::runtime_error("Cannot create Explorer menu");
 const std::wstring title=L"Synchronize with Synchrorep";
 auto status=RegSetValueExW(out,nullptr,0,REG_SZ,
  reinterpret_cast<const BYTE*>(title.c_str()),static_cast<DWORD>((title.size()+1)*sizeof(wchar_t)));
 if(status==ERROR_SUCCESS){
  wchar_t app[MAX_PATH]{};
  const auto length=GetModuleFileNameW(nullptr,app,MAX_PATH);
  if(length>0&&length<MAX_PATH){
   const auto icon=std::wstring(L"\"")+app+L"\",0";
   status=RegSetValueExW(out,L"Icon",0,REG_SZ,
    reinterpret_cast<const BYTE*>(icon.c_str()),
    static_cast<DWORD>((icon.size()+1)*sizeof(wchar_t)));
  }
 }
 RegCloseKey(out);
 if(status!=ERROR_SUCCESS)throw std::runtime_error("Cannot store Explorer menu label or icon");
 std::wstring commandKey=std::wstring(key)+L"\\command";
 if(RegCreateKeyExW(HKEY_CURRENT_USER,commandKey.c_str(),0,nullptr,0,KEY_WRITE,nullptr,&out,nullptr)!=ERROR_SUCCESS)
  throw std::runtime_error("Cannot create Explorer menu command");
 wchar_t module[32768]{};
 DWORD length=GetModuleFileNameW(nullptr,module,32768);
 if(length==0||length>=32768){RegCloseKey(out);throw std::runtime_error("Cannot locate executable");}
 const std::wstring command=L"\""+std::wstring(module)+L"\" \"%1\"";
 status=RegSetValueExW(out,nullptr,0,REG_SZ,
   reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
 RegCloseKey(out);
 if(status!=ERROR_SUCCESS)throw std::runtime_error("Cannot register Explorer menu command");
}
bool explorerMenuInstalled(){
 HKEY key=nullptr;
 if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Classes\\Directory\\shell\\Synchrorep",0,KEY_READ,&key)!=ERROR_SUCCESS)return false;
 RegCloseKey(key);return true;
}
void onCommand(int id){
 switch(id){
 case IdProfiles: {
  if(loading)break;
  auto idx=SendMessageW(controls[IdProfiles],LB_GETCURSEL,0,0);
  if(idx>=0)loadProfile(static_cast<int>(idx));
  break;
 }
 case IdNew:
  selectedProfile=-1;overrides.clear();shownDecisions.clear();
  SendMessageW(controls[IdMirror],BM_SETCHECK,BST_UNCHECKED,0);
  SendMessageW(controls[IdAutoStart],BM_SETCHECK,BST_UNCHECKED,0);
  EnableWindow(controls[IdAutoStart],FALSE);
  set(IdName,L"New synchronization");set(IdLeft,L"");set(IdRight,L"");
  clearCredentials();SendMessageW(controls[IdChanges],LB_RESETCONTENT,0,0);
  updateList();break;
 case IdSave:{
  auto p=fromForm();
  if(p.name.empty()||p.left.empty()||p.right.empty())throw std::runtime_error("Name and both locations are mandatory");
  for(std::size_t i=0;i<profiles.size();++i)
   if(static_cast<int>(i)!=selectedProfile&&profiles[i].name==p.name)
    throw std::runtime_error("Synchronization profile name already exists");
  if(p.auto_start){
   const auto target=endpoint_kind(p.right);
   if(target==EndpointKind::Ftp||target==EndpointKind::Sftp){
    if(!loadCredential(IdRight,IdRightUser,IdRightPassword))
     throw std::runtime_error("Use 'Remember logins' to store target credentials before enabling automatic mirror startup");
    if(target==EndpointKind::Sftp&&p.known_hosts.empty())
     throw std::runtime_error("SFTP automatic mirror startup requires a known_hosts file");
   }
  }
  if(p.auto_start &&
     (selectedProfile<0||!profiles[static_cast<std::size_t>(selectedProfile)].auto_start)){
   if(MessageBoxW(mainWindow,
       L"Start this mirror automatically at every Windows logon? It can overwrite or delete files on the TARGET without another confirmation. Keep recovery archives enabled and review Preview before enabling.",
       L"Enable automatic mirror startup?",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)
    break;
  }
  if(selectedProfile>=0)profiles[static_cast<std::size_t>(selectedProfile)]=p;
  else{profiles.push_back(p);selectedProfile=static_cast<int>(profiles.size()-1);}
  ProfileStore(appDirectory()/"profiles.dat").save(profiles);
  wchar_t module[32768]{};
  if(!GetModuleFileNameW(nullptr,module,32768))
   throw std::runtime_error("Cannot locate application for automatic startup");
  synchrorep::winui::synchronize_startup(profiles,module);
  updateList();break;
 }
 case IdDelete:
  if(selectedProfile>=0 && MessageBoxW(mainWindow,L"Delete this synchronization profile?",
       L"Synchrorep",MB_YESNO|MB_ICONQUESTION)==IDYES){
   profiles.erase(profiles.begin()+selectedProfile);
   ProfileStore(appDirectory()/"profiles.dat").save(profiles);
   wchar_t module[32768]{};
   if(!GetModuleFileNameW(nullptr,module,32768))
    throw std::runtime_error("Cannot locate application for automatic startup");
   synchrorep::winui::synchronize_startup(profiles,module);
   selectedProfile=-1;overrides.clear();shownDecisions.clear();set(IdName,L"");set(IdLeft,L"");set(IdRight,L"");
   updateList();
  }break;
 case IdRemember:{
  int count=static_cast<int>(storeCredential(IdLeft,IdLeftUser,IdLeftPassword))+
            static_cast<int>(storeCredential(IdRight,IdRightUser,IdRightPassword));
  std::wstring m=std::to_wstring(count)+L" account(s) stored in Windows Credential Manager.";
  MessageBoxW(mainWindow,m.c_str(),L"Synchrorep",MB_ICONINFORMATION);break;
 }
 case IdLoadCredentials:{
  int count=static_cast<int>(loadCredential(IdLeft,IdLeftUser,IdLeftPassword))+
            static_cast<int>(loadCredential(IdRight,IdRightUser,IdRightPassword));
  std::wstring m=std::to_wstring(count)+L" account(s) loaded.";
  MessageBoxW(mainWindow,m.c_str(),L"Synchrorep",MB_ICONINFORMATION);break;
 }
 case IdMirror:{
  const auto enabled=SendMessageW(controls[IdMirror],BM_GETCHECK,0,0)==BST_CHECKED;
  EnableWindow(controls[IdAutoStart],enabled);
  if(!enabled)SendMessageW(controls[IdAutoStart],BM_SETCHECK,BST_UNCHECKED,0);
  break;
 }
 case IdExplorer:
  setExplorerMenu(SendMessageW(controls[IdExplorer],BM_GETCHECK,0,0)==BST_CHECKED);break;
 case IdOpenRecovery:openRecoveryBrowser();break;
 case IdOpenLogs:{
  auto folder=appDirectory()/L"logs";
  std::filesystem::create_directories(folder);
  auto launched=reinterpret_cast<INT_PTR>(ShellExecuteW(mainWindow,L"open",
    folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
  if(launched<=32)throw std::runtime_error("Cannot open Synchrorep storage directory");
  break;
 }
 case IdPreview:startSync(false);break;
 case IdExecute:startSync(true);break;
 case IdCancel:if(job)job->stop();set(IdStatus,L"Cancellation requested; waiting for current file.");break;
 case IdKeepLeft:chooseConflict(ConflictChoice::KeepLeft);break;
 case IdKeepRight:chooseConflict(ConflictChoice::KeepRight);break;
 case IdSkip:chooseConflict(ConflictChoice::Skip);break;
 default:break;
 }
}
void initializeControls(HWND h){
 font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
 label(h,L"Configured synchronizations",18,16,285);
 place(h,L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|LBS_NOTIFY,18,43,274,430,IdProfiles);
 button(h,L"New",IdNew,18,483,84);button(h,L"Save",IdSave,109,483,84);
 button(h,L"Delete",IdDelete,200,483,91);
 place(h,L"BUTTON",L"Explorer context menu",BS_AUTOCHECKBOX|WS_TABSTOP,18,534,270,24,IdExplorer);
 SendMessageW(controls[IdExplorer],BM_SETCHECK,explorerMenuInstalled()?BST_CHECKED:BST_UNCHECKED,0);
 place(h,L"BUTTON",L"Preserve old versions",BS_AUTOCHECKBOX|WS_TABSTOP,
       18,566,274,24,IdProtect);
 SendMessageW(controls[IdProtect],BM_SETCHECK,BST_CHECKED,0);
 place(h,L"BUTTON",L"Use Windows Recycle Bin (local)",BS_AUTOCHECKBOX|WS_TABSTOP,
       18,597,274,24,IdRecycle);
 SendMessageW(controls[IdRecycle],BM_SETCHECK,BST_CHECKED,0);
 button(h,L"Browse / restore archives",IdOpenRecovery,18,637,274);
 button(h,L"Open execution logs",IdOpenLogs,18,675,274);
 place(h,L"BUTTON",L"Continuous local -> target mirror",BS_AUTOCHECKBOX|WS_TABSTOP,
       18,716,274,24,IdMirror);
 SendMessageW(controls[IdMirror],BM_SETCHECK,BST_UNCHECKED,0);
 place(h,L"BUTTON",L"Start at Windows login",BS_AUTOCHECKBOX|WS_TABSTOP,
       18,748,274,24,IdAutoStart);
 EnableWindow(controls[IdAutoStart],FALSE);
 label(h,L"Background startup per saved profile",18,777,274);

 label(h,L"Profile name",320,16);
 edit(h,IdName,320,39,546);
#ifdef SYNCHROREP_REMOTE_ENABLED
 label(h,L"Left directory / URI (local, wsl://, ftp://, sftp://)",320,77,540);
#else
 label(h,L"Left directory / URI (local, wsl://) - Lite build",320,77,540);
#endif
 edit(h,IdLeft,320,100,546);
 label(h,L"Right directory / URI",320,138);
 edit(h,IdRight,320,161,546);
 label(h,L"Remote account (left)",320,201);
 edit(h,IdLeftUser,320,226,246);
 label(h,L"Password (left)",584,201,250);
 edit(h,IdLeftPassword,584,226,282,true);
 label(h,L"Remote account (right)",320,265);
 edit(h,IdRightUser,320,290,246);
 label(h,L"Password (right)",584,265,250);
 edit(h,IdRightPassword,584,290,282,true);
 label(h,L"SFTP known_hosts file (required for SSH host verification)",320,329,545);
 edit(h,IdKnownHosts,320,352,546);
 button(h,L"Remember logins",IdRemember,320,389,185);
 button(h,L"Load saved logins",IdLoadCredentials,514,389,185);
 label(h,L"Conflict resolution",320,434);
 place(h,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,320,458,265,200,IdPolicy);
 for(const wchar_t* choice:{L"Ask / stop",L"Ignore conflicts",L"Stop on conflicts",L"Keep newest",L"Keep oldest"})
  SendMessageW(controls[IdPolicy],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(choice));
 SendMessageW(controls[IdPolicy],CB_SETCURSEL,0,0);
 place(h,L"BUTTON",L"Propagate deletions",BS_AUTOCHECKBOX|WS_TABSTOP,620,461,246,23,IdDeletes);
 SendMessageW(controls[IdDeletes],BM_SETCHECK,BST_CHECKED,0);
 button(h,L"Preview",IdPreview,320,505,244);
 button(h,L"Synchronize",IdExecute,575,505,291);
 button(h,L"Cancel operation",IdCancel,320,543,260);
 EnableWindow(controls[IdCancel],FALSE);
 place(h,L"BUTTON",L"Resume interrupted run (rescan + reconcile)",
       BS_AUTOCHECKBOX|WS_TABSTOP,592,546,274,24,IdResume);
 label(h,L"Pending operations / conflicts (select a conflict)",320,586,540);
 place(h,L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY,320,608,546,100,IdChanges);
 button(h,L"Keep left",IdKeepLeft,320,712,170);
 button(h,L"Keep right",IdKeepRight,500,712,170);
 button(h,L"Skip file",IdSkip,680,712,186);
 place(h,PROGRESS_CLASSW,L"",PBS_SMOOTH,320,750,546,18,IdProgress);
 place(h,L"STATIC",L"Ready",0,320,777,546,20,IdStatus);
}
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l){
 switch(message){
 case WM_CREATE:
  mainWindow=window;
  try{initializeControls(window);}catch(const std::exception& e){MessageBoxA(window,e.what(),"Initialization failed",MB_ICONERROR);return -1;}
  return 0;
 case WM_COMMAND:
  if(LOWORD(w)==IdProfiles&&HIWORD(w)!=LBN_SELCHANGE)return 0;
  if(LOWORD(w)==IdPolicy&&HIWORD(w)!=CBN_SELCHANGE)return 0;
  if(LOWORD(w)!=IdProfiles&&LOWORD(w)!=IdPolicy&&HIWORD(w)!=BN_CLICKED)return 0;
  try{onCommand(LOWORD(w));}catch(const std::exception& e){MessageBoxA(window,e.what(),"Synchrorep error",MB_ICONERROR);}
  return 0;
 case synchrorep::winui::ProgressMessage:
  onProgress(std::unique_ptr<synchrorep::winui::ProgressEvent>(
   reinterpret_cast<synchrorep::winui::ProgressEvent*>(l)));
  return 0;
 case synchrorep::winui::FinishedMessage:
  onFinished(std::unique_ptr<synchrorep::winui::FinishedEvent>(
   reinterpret_cast<synchrorep::winui::FinishedEvent*>(l)));
  return 0;
 case WM_CLOSE:
  if(activeMirror&&busy&&trayAdded){
   ShowWindow(window,SW_HIDE);
   return 0;
  }
  DestroyWindow(window);
  return 0;
 case WM_SYSCOMMAND:
  if((w&0xfff0)==SC_MINIMIZE && activeMirror&&busy&&trayAdded){
   ShowWindow(window,SW_HIDE);
   return 0;
  }
  return DefWindowProcW(window,message,w,l);
 case TrayMessage:{
  if(l==WM_LBUTTONDBLCLK){showMainWindow();return 0;}
  if(l==WM_RBUTTONUP){
   HMENU menu=CreatePopupMenu();
   if(!menu)return 0;
   AppendMenuW(menu,MF_STRING,901,L"Show Synchrorep");
   AppendMenuW(menu,MF_STRING,902,L"Stop continuous mirror");
   AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
   AppendMenuW(menu,MF_STRING,903,L"Exit Synchrorep");
   POINT cursor{};GetCursorPos(&cursor);
   SetForegroundWindow(window);
   const auto action=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,
                                    cursor.x,cursor.y,0,window,nullptr);
   DestroyMenu(menu);
   if(action==901)showMainWindow();
   if(action==902&&job)job->stop();
   if(action==903)DestroyWindow(window);
   return 0;
  }
  return 0;
 }
 case WM_DESTROY: {
  removeTray();
  if(job){job->stop();job->join();}
  releaseMirrorLock();
  MSG pending;
  while(PeekMessageW(&pending,window,synchrorep::winui::ProgressMessage,
                     synchrorep::winui::FinishedMessage,PM_REMOVE)){
   if(pending.message==synchrorep::winui::ProgressMessage)
    delete reinterpret_cast<synchrorep::winui::ProgressEvent*>(pending.lParam);
   else if(pending.message==synchrorep::winui::FinishedMessage)
    delete reinterpret_cast<synchrorep::winui::FinishedEvent*>(pending.lParam);
  }
  PostQuitMessage(0);
  return 0;
 }
 default:return DefWindowProcW(window,message,w,l);
 }
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
 WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=instance;
 wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));
 wc.lpszClassName=L"SynchrorepWindow";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
 wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
 INITCOMMONCONTROLSEX init{sizeof(init),ICC_PROGRESS_CLASS};
 InitCommonControlsEx(&init);
 RegisterClassW(&wc);
#ifdef SYNCHROREP_REMOTE_ENABLED
 const wchar_t* title=L"Synchrorep - Settings";
#else
 const wchar_t* title=L"Synchrorep Lite - Settings (local/WSL)";
#endif
 HWND window=CreateWindowW(wc.lpszClassName,title,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|
   WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,900,850,nullptr,nullptr,instance,nullptr);
 if(!window)return 1;
 try{
  profiles=ProfileStore(appDirectory()/"profiles.dat").load();
  int argc=0;
  LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
  std::wstring folder;
  std::optional<std::wstring> autoProfile;
  if(argv){
   if(argc>1 && std::wstring(argv[1])==L"--auto-mirror"){
    if(argc!=3){LocalFree(argv);throw std::runtime_error("Invalid automatic mirror arguments");}
    autoProfile=argv[2];
   }else if(argc>1)folder=argv[1];
   LocalFree(argv);
  }
  updateList();
  if(autoProfile){
   (void)startup_entries(profiles);
   auto wanted=toUtf8(*autoProfile);
   int found=-1;
   for(std::size_t i=0;i<profiles.size();++i)
    if(profiles[i].name==wanted){found=static_cast<int>(i);break;}
   if(found<0 || !profiles[static_cast<std::size_t>(found)].auto_start ||
      profiles[static_cast<std::size_t>(found)].mode!=SyncMode::Mirror)
    throw std::runtime_error("The requested profile is not enabled for automatic mirror startup");
   loadProfile(found);
   const auto kind=endpoint_kind(profiles[static_cast<std::size_t>(found)].right);
   if(kind==EndpointKind::Ftp || kind==EndpointKind::Sftp){
    if(!loadCredential(IdRight,IdRightUser,IdRightPassword))
     throw std::runtime_error("Automatic mirror startup requires saved destination credentials in Windows Credential Manager");
   }
   startSync(true,true);
   if(!trayAdded)ShowWindow(window,SW_SHOWNORMAL);
  }else{
   if(!folder.empty()){
    auto needle=toUtf8(folder);int found=-1;
    for(std::size_t i=0;i<profiles.size();++i)
     if(profiles[i].left==needle||profiles[i].right==needle){found=static_cast<int>(i);break;}
    if(found>=0)loadProfile(found);
    else{set(IdName,L"New synchronization");set(IdLeft,folder);}
   }else if(!profiles.empty())loadProfile(0);
   else{
    set(IdName,L"New synchronization");
    wchar_t user[32768]{};DWORD n=GetEnvironmentVariableW(L"USERPROFILE",user,32768);
    if(n>0&&n<32768)set(IdKnownHosts,std::wstring(user)+L"\\.ssh\\known_hosts");
   }
   // Update Run paths if this executable was moved or upgraded.
   wchar_t module[32768]{};
   const auto length=GetModuleFileNameW(nullptr,module,32768);
   if(length>0&&length<32768)
    synchrorep::winui::synchronize_startup(profiles,module);
   ShowWindow(window,show);
  }
 }catch(const std::exception& e){
  ShowWindow(window,SW_SHOWNORMAL);
  MessageBoxA(window,e.what(),"Synchrorep startup error",MB_ICONERROR);
 }
 MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
 return 0;
}
