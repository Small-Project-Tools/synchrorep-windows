// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#define UNICODE
#define _UNICODE
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <curl/curl.h>
#include <cwchar>
#include <cstring>
#include <commctrl.h>
#include <atomic>
#include <thread>
#include <map>
#include <optional>
#include <vector>
#include "async_job.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
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
#include "synchrorep/sqlite_history.hpp"
#include "synchrorep/mirror_monitor.hpp"
#include "startup_registry.hpp"
#include "synchrorep/startup_profiles.hpp"
#include "synchrorep/explorer_launch.hpp"
#include "ui_layout.hpp"
#include "ui_theme.hpp"

namespace {
using namespace synchrorep;
enum Control : int {
 IdProfiles=101,IdNew=102,IdSave=103,IdDelete=104,IdName=105,
 IdLeft=106,IdRight=107,IdLeftUser=108,IdLeftPassword=109,
 IdRightUser=110,IdRightPassword=111,IdKnownHosts=112,
 IdRemember=113,IdLoadCredentials=114,IdPolicy=115,IdDeletes=116,
 IdPreview=117,IdExecute=118,IdChanges=119,IdExplorer=120,
 IdCancel=121,IdKeepLeft=122,IdKeepRight=123,IdSkip=124,IdProgress=125,IdStatus=126,IdResume=127,
 IdProtect=128,IdOpenRecovery=129,IdOpenLogs=130,IdRecycle=131,IdMirror=132,IdAutoStart=133,IdVerifyHash=134
};
enum ExtraControl : int {
 IdTabConfiguration=200,IdTabPreview=201,IdTabHistory=202,IdTabSettings=203,
 IdBrowseSource=204,IdBrowseDestination=205,IdModeBidirectional=206,IdModeMirror=207,
 IdSettingsNetwork=208,IdSettingsBehavior=209,
 IdLabelName=210,IdLabelSource=211,IdLabelDestination=212,IdLabelMode=213,
 IdLabelOptions=214,IdLabelLeftUser=215,IdLabelLeftPassword=216,
 IdLabelRightUser=217,IdLabelRightPassword=218,IdLabelHosts=219,
 IdLabelPolicy=220,IdSummaryTotal=221,IdSummaryCopies=222,
 IdSummaryUpdates=223,IdSummaryDeletes=224,IdSummaryConflicts=225,
 IdHistoryInfo=226,IdPreviewInfo=227,IdNetworkInfo=228,IdBehaviorInfo=229,
 IdConfigurationHint=230,IdSummaryHint=231
};
enum class MainTab {Configuration,Preview,History,Settings};
enum class SettingsPane {Network,Behavior};
MainTab activeTab=MainTab::Configuration;
SettingsPane settingsPane=SettingsPane::Network;
synchrorep::winui::UiLayout mainLayout{};
HWND controls[256]{};
HWND mainWindow=nullptr;
HWND syncWindow=nullptr;
HWND syncProgress=nullptr;
HWND syncStatus=nullptr;
HWND syncChanges=nullptr;
HWND syncCancel=nullptr;
bool shuttingDown=false;
HFONT font=nullptr;
std::vector<Profile> profiles;
int selectedProfile=-1;
bool loading=false;
std::unique_ptr<synchrorep::winui::AsyncJob> job;
bool busy=false;
double progressFraction=0.0;
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
LRESULT CALLBACK synchronizationWindowProc(HWND window,UINT message,WPARAM w,LPARAM l){
 switch(message){
 case WM_COMMAND:
  if(LOWORD(w)==IdCancel&&job){job->stop();return 0;}
  return 0;
 case WM_ERASEBKGND:return 1;
 case WM_PAINT: {
  PAINTSTRUCT paint{};
  HDC dc=BeginPaint(window,&paint);
  RECT area{};GetClientRect(window,&area);
  FillRect(dc,&area,synchrorep::winui::ui_card_brush());
  EndPaint(window,&paint);return 0;
 }
 case WM_DRAWITEM: {
  const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);
  if(item->CtlType==ODT_BUTTON){
   synchrorep::winui::ui_draw_button(*item,false,false,true);
   return TRUE;
  }
  return FALSE;
 }
 case WM_CTLCOLORSTATIC: {
  HDC dc=reinterpret_cast<HDC>(w);
  SetBkColor(dc,synchrorep::winui::color::card);
  SetTextColor(dc,synchrorep::winui::color::white);
  return reinterpret_cast<LRESULT>(synchrorep::winui::ui_card_brush());
 }
 case WM_CTLCOLORLISTBOX: {
  HDC dc=reinterpret_cast<HDC>(w);
  SetBkColor(dc,synchrorep::winui::color::input);
  SetTextColor(dc,synchrorep::winui::color::white);
  return reinterpret_cast<LRESULT>(synchrorep::winui::ui_input_brush());
 }
 case WM_CLOSE:
  if(busy){
   if(MessageBoxW(window,L"Stop the running synchronization?",L"Synchrorep",
                  MB_YESNO|MB_ICONQUESTION)==IDYES&&job)job->stop();
   return 0;
  }
  DestroyWindow(window);return 0;
 case WM_DESTROY:
  if(syncWindow==window){syncWindow=nullptr;syncProgress=nullptr;
    syncStatus=nullptr;syncChanges=nullptr;syncCancel=nullptr;
   if(!shuttingDown&&mainWindow && !IsWindowVisible(mainWindow) && !busy)ShowWindow(mainWindow,SW_SHOW);}
  return 0;
 default:return DefWindowProcW(window,message,w,l);
 }
}
void openSynchronizationWindow(){
 if(syncWindow){ShowWindow(syncWindow,SW_RESTORE);SetForegroundWindow(syncWindow);
  if(syncCancel)EnableWindow(syncCancel,TRUE);
  if(syncProgress)SendMessageW(syncProgress,PBM_SETPOS,0,0);
  if(syncChanges)SendMessageW(syncChanges,LB_RESETCONTENT,0,0);
  return;}
 static bool registered=false;
 auto module=GetModuleHandleW(nullptr);
 if(!registered){
  WNDCLASSW wc{};wc.lpfnWndProc=synchronizationWindowProc;
  wc.hInstance=module;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
  wc.hIcon=LoadIconW(module,MAKEINTRESOURCEW(101));
  wc.hbrBackground=synchrorep::winui::ui_card_brush();
  wc.lpszClassName=L"SynchrorepSynchronization";
  if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
   throw std::runtime_error("Cannot register synchronization window");
  registered=true;
 }
 syncWindow=CreateWindowW(L"SynchrorepSynchronization",
  L"Synchrorep - Synchronization",WS_OVERLAPPEDWINDOW,
  CW_USEDEFAULT,CW_USEDEFAULT,730,445,nullptr,nullptr,module,nullptr);
 if(!syncWindow)throw std::runtime_error("Cannot create synchronization window");
 synchrorep::winui::ui_set_dark_title(syncWindow);
 syncStatus=CreateWindowW(L"STATIC",L"Preparing synchronization...",WS_CHILD|WS_VISIBLE,
  20,18,670,40,syncWindow,nullptr,module,nullptr);
 syncProgress=CreateWindowW(PROGRESS_CLASSW,L"",WS_CHILD|WS_VISIBLE|PBS_SMOOTH,
  20,64,670,24,syncWindow,nullptr,module,nullptr);
 syncChanges=CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|WS_HSCROLL,
  20,104,670,220,syncWindow,nullptr,module,nullptr);
 syncCancel=CreateWindowW(L"BUTTON",L"Cancel operation",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
  20,340,210,32,syncWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdCancel)),module,nullptr);
 for(HWND h:{syncStatus,syncChanges,syncCancel})
  if(h)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
 synchrorep::winui::ui_style_button(syncCancel);
 ShowWindow(syncWindow,SW_SHOWNORMAL);
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
 if(trayAdded){
  info.uVersion=NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION,&info);
 }
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
std::string storedIdentity(const std::string& uri){
 const auto kind=endpoint_kind(uri);
 if(kind==EndpointKind::Ftp||kind==EndpointKind::Sftp){
  const auto start=uri.find('/',uri.find("://")+3);
  std::string normalized=uri;
  if(start==std::string::npos||normalized.back()!='/')normalized+='/';
  return normalized;
 }
 const auto path=kind==EndpointKind::Wsl?wsl_unc_path(uri):
  std::filesystem::path(toWide(uri));
 auto normalized=std::filesystem::absolute(path).lexically_normal().generic_u8string();
 return "file:"+std::string(normalized.begin(),normalized.end());
}
std::filesystem::path profileStatePath(const Profile& profile){
 const auto key=storedIdentity(profile.left)+"\n"+storedIdentity(profile.right);
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
 if(std::wcscmp(type,L"BUTTON")==0) {
  if((style&BS_TYPEMASK)==BS_AUTOCHECKBOX)synchrorep::winui::ui_style_checkbox(win);
  else if((style&BS_TYPEMASK)==BS_OWNERDRAW)synchrorep::winui::ui_style_button(win);
 }
 return win;
}
void label(HWND h,const wchar_t* text,int x,int y,int w=350){
 place(h,L"STATIC",text,0,x,y,w,20);
}
void edit(HWND h,int id,int x,int y,int w,bool password=false){
 place(h,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|(password?ES_PASSWORD:0),
       x,y,w,25,id);
}
void button(HWND h,const wchar_t* text,int id,int x,int y,int w){
 place(h,L"BUTTON",text,WS_TABSTOP|BS_OWNERDRAW,x,y,w,30,id);
}
void namedLabel(HWND h,const wchar_t* text,int id) {
 place(h,L"STATIC",text,0,0,0,180,20,id);
}
void moveControl(int id,int x,int y,int w,int h) {
 if(controls[id])MoveWindow(controls[id],x,y,std::max(1,w),std::max(1,h),TRUE);
}
std::optional<std::wstring> chooseDestinationFolder(HWND owner,const std::wstring& source);
void refreshSummary();
void showTab(MainTab tab);
void reflowControls(HWND window);
void updateModeDescription();
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
 InvalidateRect(mainWindow,nullptr,FALSE);
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
 SendMessageW(controls[IdVerifyHash],BM_SETCHECK,p.verify_hash?BST_CHECKED:BST_UNCHECKED,0);
 EnableWindow(controls[IdAutoStart],p.mode==SyncMode::Mirror);
 SendMessageW(controls[IdChanges],LB_RESETCONTENT,0,0);
 clearCredentials();updateList();refreshSummary();updateModeDescription();
 InvalidateRect(controls[IdModeBidirectional],nullptr,FALSE);
 InvalidateRect(controls[IdModeMirror],nullptr,FALSE);
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
 p.verify_hash=SendMessageW(controls[IdVerifyHash],BM_GETCHECK,0,0)==BST_CHECKED;
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
 RemoteCredentials credentials{username,password,known_hosts,{}};
 if(credentials.username.empty())throw std::runtime_error("Remote username is required");
 return std::make_unique<RemoteEndpoint>(uri,std::move(credentials));
}
void showPlan(const std::vector<Decision>& plan){
 shownDecisions.clear();
 SendMessageW(controls[IdChanges],LB_RESETCONTENT,0,0);
 if(syncChanges)SendMessageW(syncChanges,LB_RESETCONTENT,0,0);
 for(const auto& d:plan){
  if(d.action==Action::None)continue;
  shownDecisions.push_back(d);
  auto text=operationName(d.action)+L" : "+toWide(d.relative);
  SendMessageW(controls[IdChanges],LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
  if(syncChanges)SendMessageW(syncChanges,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
 }
 refreshSummary();
}
void refreshSummary(){
 int copied=0,updated=0,removed=0,conflicts=0;
 for(const auto& d:shownDecisions) {
  if(d.action==Action::CopyLeftToRight||d.action==Action::CopyRightToLeft)
   ++copied;
  else if(d.action==Action::DeleteLeft||d.action==Action::DeleteRight)
   ++removed;
  else if(d.action==Action::Conflict)++conflicts;
 }
 // The engine describes copy/update with the same action. Do not fabricate
 // separate update counts until the plan model distinguishes these operations.
 set(IdSummaryTotal,L"Planned actions  "+std::to_wstring(shownDecisions.size()));
 set(IdSummaryCopies,L"Transfers  "+std::to_wstring(copied));
 set(IdSummaryUpdates,L"Updates  —");
 set(IdSummaryDeletes,L"Deletions  "+std::to_wstring(removed));
 set(IdSummaryConflicts,L"Conflicts  "+std::to_wstring(conflicts));
}
void setBusy(bool running){
 busy=running;
 for(int id:std::initializer_list<int>{IdProfiles,IdNew,IdSave,IdDelete,IdName,IdLeft,IdRight,
  IdLeftUser,IdLeftPassword,IdRightUser,IdRightPassword,IdKnownHosts,
  IdRemember,IdLoadCredentials,IdPolicy,IdDeletes,IdPreview,IdExecute,
  IdExplorer,IdKeepLeft,IdKeepRight,IdSkip,IdResume,IdProtect,IdOpenRecovery,
  IdOpenLogs,IdRecycle,IdMirror,IdAutoStart,IdVerifyHash,IdBrowseSource,
  IdBrowseDestination,IdModeBidirectional,IdModeMirror})
   EnableWindow(controls[id],!running);
 EnableWindow(controls[IdCancel],running);
 if(!running)
  EnableWindow(controls[IdAutoStart],
   SendMessageW(controls[IdMirror],BM_GETCHECK,0,0)==BST_CHECKED);
 InvalidateRect(mainWindow,nullptr,FALSE);
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
 progressFraction=0.0;
 set(IdStatus,L"Starting...");
 setBusy(true);
 const bool mirrorMode=settings.profile.mode==SyncMode::Mirror&&execute;
 try{
  openSynchronizationWindow();
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
                    settings.profile.recycle_local_deletions,settings.profile.mode,
                    settings.profile.verify_hash};
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
 progressFraction=info.total?std::min(1.0,static_cast<double>(info.completed)/info.total):0.0;
 InvalidateRect(mainWindow,nullptr,FALSE);
 if(syncProgress){
  SendMessageW(syncProgress,PBM_SETRANGE32,0,std::max<LPARAM>(1,range));
  SendMessageW(syncProgress,PBM_SETPOS,static_cast<WPARAM>(std::min<std::size_t>(info.completed,1000000)),0);
 }
 auto text=phaseName(info.phase)+L" "+
  std::to_wstring(info.completed)+L"/"+std::to_wstring(info.total);
 if(!info.relative.empty())text+=L" : "+toWide(info.relative);
 set(IdStatus,text);
 if(syncStatus)SetWindowTextW(syncStatus,text.c_str());
}
void onFinished(std::unique_ptr<synchrorep::winui::FinishedEvent> result){
 if(job)job->join();
 const bool wasMirror=activeMirror;
 activeMirror=false;
 removeTray();
 releaseMirrorLock();
 if(wasMirror&&!IsWindowVisible(mainWindow))showMainWindow();
 setBusy(false);
 if(syncCancel)EnableWindow(syncCancel,FALSE);
 showPlan(result->decisions);
 if(result->cancelled){
  set(IdStatus,L"Cancelled between files; completed changes remain.");
  MessageBoxW(syncWindow?syncWindow:mainWindow,L"Cancelled. Completed transfers are preserved. The next run will reconcile them.",
              L"Synchrorep",MB_OK|MB_ICONWARNING);
 }else if(!result->error.empty()){
  set(IdStatus,L"Operation failed or conflicts unresolved.");
  MessageBoxA(syncWindow?syncWindow:mainWindow,result->error.c_str(),"Synchrorep",MB_OK|MB_ICONERROR);
 }else if(result->executed){
  auto text=L"Copied: "+std::to_wstring(result->result.copied)+
            L"\nDeleted: "+std::to_wstring(result->result.removed)+
            L"\nRecovery copies: "+std::to_wstring(result->result.archived)+
            L"\nRecycled: "+std::to_wstring(result->result.recycled)+
            L"\nConflicts: "+std::to_wstring(result->result.conflicts.size());
  set(IdStatus,wasMirror?L"Continuous mirror stopped.":L"Synchronization complete.");
  MessageBoxW(syncWindow?syncWindow:mainWindow,text.c_str(),L"Synchrorep",MB_OK|MB_ICONINFORMATION);
 }else {
  set(IdStatus,L"Preview complete. Select a conflict to choose its resolution.");
  showTab(MainTab::Preview);
 }
 if(syncStatus)SetWindowTextW(syncStatus,get(IdStatus).c_str());
 if(!wasMirror&&!IsWindowVisible(mainWindow)&&!syncWindow)showMainWindow();
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
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{
  PAINTSTRUCT paint{};
  HDC dc=BeginPaint(window,&paint);
  RECT area{};GetClientRect(window,&area);
  FillRect(dc,&area,synchrorep::winui::ui_card_brush());
  EndPaint(window,&paint);return 0;
 }
 case WM_CTLCOLOREDIT:
 case WM_CTLCOLORLISTBOX:{
  HDC dc=reinterpret_cast<HDC>(w);
  SetBkColor(dc,synchrorep::winui::color::input);
  SetTextColor(dc,synchrorep::winui::color::white);
  return reinterpret_cast<LRESULT>(synchrorep::winui::ui_input_brush());
 }
 case WM_CTLCOLORSTATIC:{
  HDC dc=reinterpret_cast<HDC>(w);
  SetBkColor(dc,synchrorep::winui::color::card);
  SetTextColor(dc,synchrorep::winui::color::white);
  return reinterpret_cast<LRESULT>(synchrorep::winui::ui_card_brush());
 }
 case WM_DRAWITEM:{
  const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);
  if(item->CtlType==ODT_BUTTON) {
   synchrorep::winui::ui_draw_button(*item,false,item->CtlID==RecoveryExport,false);
   return TRUE;
  }
  return FALSE;
 }
 case WM_CREATE:{
  CreateWindowW(L"STATIC",L"Archived versions (read-only; exports never overwrite files)",
    WS_CHILD|WS_VISIBLE,16,15,735,22,window,nullptr,GetModuleHandleW(nullptr),nullptr);
  recoveryList=CreateWindowExW(0,L"LISTBOX",L"",
    WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY,
    16,43,750,318,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(RecoveryList)),
    GetModuleHandleW(nullptr),nullptr);
  recoveryInfo=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,16,372,745,28,
    window,nullptr,GetModuleHandleW(nullptr),nullptr);
  CreateWindowW(L"BUTTON",L"Refresh",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
    16,415,145,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(RecoveryRefresh)),
    GetModuleHandleW(nullptr),nullptr);
  CreateWindowW(L"BUTTON",L"Export selected",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
    173,415,185,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(RecoveryExport)),
    GetModuleHandleW(nullptr),nullptr);
  CreateWindowW(L"BUTTON",L"Open archive folder",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
    370,415,194,34,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(RecoveryFolder)),
    GetModuleHandleW(nullptr),nullptr);
  for(int id:{RecoveryRefresh,RecoveryExport,RecoveryFolder}) {
   HWND button=GetDlgItem(window,id);
   if(button){
    SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    synchrorep::winui::ui_style_button(button);
   }
  }
  for(HWND child:{recoveryList,recoveryInfo})
   if(child)SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
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
  cls.hbrBackground=synchrorep::winui::ui_card_brush();
  cls.lpszClassName=L"SynchrorepRecovery";
  if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
   throw std::runtime_error("Could not register recovery window");
  registered=true;
 }
 recoveryWindow=CreateWindowW(L"SynchrorepRecovery",L"Synchrorep - Recover previous versions",
   WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
   CW_USEDEFAULT,CW_USEDEFAULT,800,505,mainWindow,nullptr,module,nullptr);
 if(!recoveryWindow)throw std::runtime_error("Could not create recovery window");
 synchrorep::winui::ui_set_dark_title(recoveryWindow);
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
bool explorerMenuMachineInstalled(){
 HKEY key=nullptr;
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"Software\\Classes\\Directory\\shell\\Synchrorep\\command",
                  0,KEY_READ,&key)!=ERROR_SUCCESS)return false;
 RegCloseKey(key);
 return true;
}
bool explorerMenuInstalled(){
 if(explorerMenuMachineInstalled())return true;
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
  if(busy)break;
  selectedProfile=-1;overrides.clear();shownDecisions.clear();
  SendMessageW(controls[IdMirror],BM_SETCHECK,BST_UNCHECKED,0);
  SendMessageW(controls[IdAutoStart],BM_SETCHECK,BST_UNCHECKED,0);
  EnableWindow(controls[IdAutoStart],FALSE);
  SendMessageW(controls[IdPolicy],CB_SETCURSEL,0,0);
  for(int id:{IdDeletes,IdProtect,IdRecycle})
   SendMessageW(controls[id],BM_SETCHECK,BST_CHECKED,0);
  SendMessageW(controls[IdVerifyHash],BM_SETCHECK,BST_UNCHECKED,0);
  set(IdKnownHosts,L"");
  set(IdName,L"New synchronization");set(IdLeft,L"");set(IdRight,L"");
  clearCredentials();SendMessageW(controls[IdChanges],LB_RESETCONTENT,0,0);
  updateList();refreshSummary();updateModeDescription();showTab(MainTab::Configuration);break;
 case IdSave:{
  if(busy)throw std::runtime_error("Cannot edit profiles during synchronization");
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
  if(busy)throw std::runtime_error("Cannot delete a profile while synchronization is running");
  if(selectedProfile>=0 && MessageBoxW(mainWindow,
       L"Delete this synchronization profile and its stored file history? Files on disk are not deleted.",
       L"Synchrorep",MB_YESNO|MB_ICONQUESTION)==IDYES){
   const auto removed=profiles[static_cast<std::size_t>(selectedProfile)];
   const auto state=profileStatePath(removed);
   auto remaining=profiles;
   remaining.erase(remaining.begin()+selectedProfile);
   bool shared=false;
   for(const auto& p:remaining)
    if(profileStatePath(p)==state){shared=true;break;}
   // Save the new profile set before removing the orphaned history.
   ProfileStore(appDirectory()/"profiles.dat").save(remaining);
   profiles=std::move(remaining);
   if(!shared){
    auto sqlite=state;sqlite+=L".sqlite";
    SqliteHistory(sqlite).erase_pair(storedIdentity(removed.left),storedIdentity(removed.right));
    // Prevent a later recreation of the same profile from importing stale
    // legacy history after its SQLite record has been deleted.
    std::error_code ec;
    std::filesystem::remove(state,ec);
    if(ec)throw std::filesystem::filesystem_error("Cannot clear legacy history",state,ec);
   }
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
  updateModeDescription();
  InvalidateRect(controls[IdModeBidirectional],nullptr,FALSE);
  InvalidateRect(controls[IdModeMirror],nullptr,FALSE);
  break;
 }
 case IdModeBidirectional:
 case IdModeMirror:{
  if(busy)break;
  SendMessageW(controls[IdMirror],BM_SETCHECK,
    id==IdModeMirror?BST_CHECKED:BST_UNCHECKED,0);
  onCommand(IdMirror);
  break;
 }
 case IdBrowseSource:
 case IdBrowseDestination:{
  if(busy)break;
  const auto location=chooseDestinationFolder(mainWindow,
   id==IdBrowseSource?L"source":L"destination");
  if(location)set(id==IdBrowseSource?IdLeft:IdRight,*location);
  break;
 }
 case IdTabConfiguration:showTab(MainTab::Configuration);break;
 case IdTabPreview:showTab(MainTab::Preview);break;
 case IdTabHistory:showTab(MainTab::History);break;
 case IdTabSettings:showTab(MainTab::Settings);break;
 case IdSettingsNetwork:settingsPane=SettingsPane::Network;showTab(MainTab::Settings);break;
 case IdSettingsBehavior:settingsPane=SettingsPane::Behavior;showTab(MainTab::Settings);break;
 case IdExplorer:
  if(!explorerMenuMachineInstalled())
   setExplorerMenu(SendMessageW(controls[IdExplorer],BM_GETCHECK,0,0)==BST_CHECKED);
  break;
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
// Navigation does not recreate controls: existing profiles, credentials, preview
// decisions and active jobs retain their native Win32 handles and state.
void showTab(MainTab tab){
 activeTab=tab;
 for(HWND control:controls)
  if(control)ShowWindow(control,SW_HIDE);
 auto visible=[](std::initializer_list<int> ids){
  for(const int id:ids)if(controls[id])ShowWindow(controls[id],SW_SHOWNA);
 };
 visible({IdProfiles,IdNew,IdDelete,IdSave,IdPreview,IdExecute,IdCancel,
          IdTabConfiguration,IdTabPreview,IdTabHistory,IdTabSettings});
 if(mainLayout.showSummary)
  visible({IdStatus,IdSummaryTotal,IdSummaryCopies,IdSummaryUpdates,
           IdSummaryDeletes,IdSummaryConflicts,IdSummaryHint});
 else visible({IdStatus});
 switch(tab){
 case MainTab::Configuration:
  visible({IdLabelName,IdName,IdLabelSource,IdLeft,IdBrowseSource,
           IdLabelDestination,IdRight,IdBrowseDestination,IdLabelMode,
           IdModeBidirectional,IdModeMirror,IdLabelOptions,IdDeletes,
           IdProtect,IdRecycle,IdVerifyHash});
  break;
 case MainTab::Preview:
  visible({IdPreviewInfo,IdChanges,IdKeepLeft,IdKeepRight,IdSkip});
  break;
 case MainTab::History:
  visible({IdHistoryInfo,IdOpenRecovery,IdOpenLogs});
  break;
 case MainTab::Settings:
  visible({IdSettingsNetwork,IdSettingsBehavior});
  if(settingsPane==SettingsPane::Network)
   visible({IdLabelLeftUser,IdLeftUser,IdLabelLeftPassword,IdLeftPassword,
            IdLabelRightUser,IdRightUser,IdLabelRightPassword,IdRightPassword,
            IdLabelHosts,IdKnownHosts,IdRemember,IdLoadCredentials,IdNetworkInfo});
  else
   visible({IdLabelPolicy,IdPolicy,IdMirror /* hidden below */,
            IdAutoStart,IdResume,IdExplorer,IdBehaviorInfo});
  break;
 }
 // The original checkbox remains the source of truth for the persisted mode;
 // segmented controls operate it, but it is never displayed or focused.
 ShowWindow(controls[IdMirror],SW_HIDE);
 if(tab==MainTab::Configuration && mainLayout.content.h>=405)
  visible({IdConfigurationHint});
 if(mainLayout.showSummary)visible({IdSummaryHint});
 for(int id:{IdTabConfiguration,IdTabPreview,IdTabHistory,IdTabSettings,
             IdSettingsNetwork,IdSettingsBehavior,IdModeBidirectional,IdModeMirror})
  if(controls[id])InvalidateRect(controls[id],nullptr,FALSE);
 InvalidateRect(mainWindow,nullptr,FALSE);
}

void reflowControls(HWND window) {
 RECT client{};GetClientRect(window,&client);
 mainLayout=synchrorep::winui::compute_layout(client.right,client.bottom);
 const auto& l=mainLayout;
 const auto& side=l.sidebar;
 const auto& panel=l.content;
 const auto& footer=l.footer;
 const int sx=side.x+13, sy=side.y+48, sw=side.w-26;
 moveControl(IdProfiles,sx,sy,sw,std::max(75,side.h-110));
 const int actionsY=side.y+side.h-49;
 moveControl(IdNew,sx,actionsY,(sw-8)/2,33);
 moveControl(IdDelete,sx+(sw-8)/2+8,actionsY,(sw-8)/2,33);

 const int cx=panel.x+16,cy=panel.y,cw=panel.w-32;
 const auto formY=[&](int offset){
  return cy+synchrorep::winui::compact_form_y(l,offset);
 };
 // Keep all four tab buttons visible, also at 12-inch widths.
 const int tabWidth=std::min(156,(l.tabs.w-36)/4);
 const int tabX=16,tabY=l.tabs.y+5;
 moveControl(IdTabConfiguration,tabX,tabY,tabWidth,34);
 moveControl(IdTabPreview,tabX+tabWidth+6,tabY,tabWidth,34);
 moveControl(IdTabHistory,tabX+2*(tabWidth+6),tabY,tabWidth,34);
 moveControl(IdTabSettings,tabX+3*(tabWidth+6),tabY,tabWidth,34);
 // Main form: all mandatory configuration fits at 880 x 520 client pixels.
 moveControl(IdLabelName,cx,formY(43),cw,19);
 moveControl(IdName,cx,formY(63),cw,29);
 moveControl(IdLabelSource,cx,formY(99),cw,19);
 moveControl(IdLeft,cx,formY(119),cw-46,29);
 moveControl(IdBrowseSource,cx+cw-39,formY(119),39,29);
 moveControl(IdLabelDestination,cx,formY(154),cw,19);
 moveControl(IdRight,cx,formY(174),cw-46,29);
 moveControl(IdBrowseDestination,cx+cw-39,formY(174),39,29);
 moveControl(IdLabelMode,cx,formY(211),cw,20);
 const int segment=(cw-10)/2;
 moveControl(IdModeBidirectional,cx,formY(233),segment,33);
 moveControl(IdModeMirror,cx+segment+10,formY(233),cw-segment-10,33);
 moveControl(IdLabelOptions,cx,formY(274),cw,21);
 const int second=cx+std::max(184,cw/2);
 moveControl(IdDeletes,cx,formY(299),std::max(165,second-cx-8),24);
 moveControl(IdProtect,second,formY(299),std::max(170,cx+cw-second),24);
 moveControl(IdRecycle,cx,formY(323),std::max(165,second-cx-8),24);
 moveControl(IdVerifyHash,second,formY(323),std::max(170,cx+cw-second),24);
 moveControl(IdConfigurationHint,cx,formY(371),cw,
             std::max(24,panel.h-synchrorep::winui::compact_form_y(l,371)-18));
 // The preview has its own dedicated list and conflict resolution actions.
 moveControl(IdPreviewInfo,cx,formY(45),cw,32);
 moveControl(IdChanges,cx,formY(80),cw,std::max(80,panel.h-145));
 const int conflictY=cy+panel.h-51;
 const int third=(cw-16)/3;
 moveControl(IdKeepLeft,cx,conflictY,third,33);
 moveControl(IdKeepRight,cx+third+8,conflictY,third,33);
 moveControl(IdSkip,cx+2*third+16,conflictY,cw-2*third-16,33);

 moveControl(IdHistoryInfo,cx,formY(50),cw,62);
 moveControl(IdOpenRecovery,cx,formY(134),std::min(cw,300),38);
 moveControl(IdOpenLogs,cx,formY(188),std::min(cw,300),38);

 moveControl(IdSettingsNetwork,cx,formY(44),(cw-10)/2,32);
 moveControl(IdSettingsBehavior,cx+(cw-10)/2+10,formY(44),(cw-10)/2,32);
 const int half=(cw-12)/2;
 moveControl(IdLabelLeftUser,cx,formY(84),half,19);
 moveControl(IdLabelLeftPassword,cx+half+12,formY(84),half,19);
 moveControl(IdLeftUser,cx,formY(104),half,29);
 moveControl(IdLeftPassword,cx+half+12,formY(104),half,29);
 moveControl(IdLabelRightUser,cx,formY(144),half,19);
 moveControl(IdLabelRightPassword,cx+half+12,formY(144),half,19);
 moveControl(IdRightUser,cx,formY(164),half,29);
 moveControl(IdRightPassword,cx+half+12,formY(164),half,29);
 moveControl(IdLabelHosts,cx,formY(204),cw,19);
 moveControl(IdKnownHosts,cx,formY(224),cw,29);
 moveControl(IdRemember,cx,formY(267),half,34);
 moveControl(IdLoadCredentials,cx+half+12,formY(267),half,34);
 moveControl(IdNetworkInfo,cx,formY(313),cw,31);
 moveControl(IdLabelPolicy,cx,formY(86),cw,20);
 moveControl(IdPolicy,cx,formY(109),cw,180);
 moveControl(IdAutoStart,cx,formY(162),cw,26);
 moveControl(IdResume,cx,formY(198),cw,26);
 moveControl(IdExplorer,cx,formY(234),cw,26);
 moveControl(IdBehaviorInfo,cx,formY(282),cw,43);
 moveControl(IdMirror,0,0,1,1);

 const int fy=footer.y+10;
 const int fw=footer.w;
 const int buttonWidth[4]={108,112,142,98};
 const int total=108+112+142+98+3*9;
 int fx=footer.x+fw-total-12;
 moveControl(IdSave,fx,fy,buttonWidth[0],36);fx+=buttonWidth[0]+9;
 moveControl(IdPreview,fx,fy,buttonWidth[1],36);fx+=buttonWidth[1]+9;
 moveControl(IdExecute,fx,fy,buttonWidth[2],36);fx+=buttonWidth[2]+9;
 moveControl(IdCancel,fx,fy,buttonWidth[3],36);
 moveControl(IdProgress,footer.x+14,footer.y+41,std::max(170,fw-total-42),5);
 if(l.showSummary) {
  const auto& r=l.summary;
  const int rx=r.x+16;
  const int width=r.w-32;
  moveControl(IdStatus,rx,r.y+54,width,32);
  moveControl(IdSummaryTotal,rx,r.y+114,width,28);
  moveControl(IdSummaryCopies,rx,r.y+154,width,25);
  moveControl(IdSummaryUpdates,rx,r.y+183,width,25);
  moveControl(IdSummaryDeletes,rx,r.y+212,width,25);
  moveControl(IdSummaryConflicts,rx,r.y+241,width,25);
  moveControl(IdSummaryHint,rx,r.y+283,width,std::max(22,r.h-301));
 }else moveControl(IdStatus,footer.x+16,footer.y+10,
                   std::max(120,fw-total-42),31);
 showTab(activeTab);
}
void initializeControls(HWND h){
 font=synchrorep::winui::ui_font();
 synchrorep::winui::ui_set_dark_title(h);
 // Sidebar (owner drawn to avoid the system light selection highlight).
 place(h,L"LISTBOX",L"",LBS_NOTIFY|LBS_HASSTRINGS|LBS_OWNERDRAWFIXED|
   WS_VSCROLL|WS_TABSTOP,0,0,150,150,IdProfiles);
 button(h,L"+ New",IdNew,0,0,85);
 button(h,L"Delete",IdDelete,0,0,85);
 button(h,L"Save profile",IdSave,0,0,108);
 button(h,L"Preview",IdPreview,0,0,108);
 button(h,L"Start sync",IdExecute,0,0,140);
 button(h,L"Stop",IdCancel,0,0,98);
 EnableWindow(controls[IdCancel],FALSE);

 for(const auto& entry:{
  std::pair{IdTabConfiguration,L"Configuration"},
  std::pair{IdTabPreview,L"Preview"},
  std::pair{IdTabHistory,L"History"},
  std::pair{IdTabSettings,L"Settings"}})
  button(h,entry.second,entry.first,0,0,144);

 namedLabel(h,L"Profile name",IdLabelName);
 edit(h,IdName,0,0,200);
 namedLabel(h,L"Source   ·   local, wsl://, ftp://, sftp://",IdLabelSource);
 edit(h,IdLeft,0,0,200);
 button(h,L"...",IdBrowseSource,0,0,38);
 namedLabel(h,L"Destination   ·   local, WSL, FTP, SFTP",IdLabelDestination);
 edit(h,IdRight,0,0,200);
 button(h,L"...",IdBrowseDestination,0,0,38);
 namedLabel(h,L"Synchronization mode",IdLabelMode);
 button(h,L"Bidirectional",IdModeBidirectional,0,0,160);
 button(h,L"Continuous mirror",IdModeMirror,0,0,160);
 namedLabel(h,L"Safety and verification",IdLabelOptions);
 place(h,L"STATIC",L"",0,0,0,460,54,IdConfigurationHint);
 place(h,L"BUTTON",L"Propagate deletions",
  BS_AUTOCHECKBOX|WS_TABSTOP,0,0,175,24,IdDeletes);
 place(h,L"BUTTON",L"Keep previous versions",
  BS_AUTOCHECKBOX|WS_TABSTOP,0,0,175,24,IdProtect);
 place(h,L"BUTTON",L"Use Recycle Bin (local)",
  BS_AUTOCHECKBOX|WS_TABSTOP,0,0,175,24,IdRecycle);
 place(h,L"BUTTON",L"Verify SHA-256",
  BS_AUTOCHECKBOX|WS_TABSTOP,0,0,175,24,IdVerifyHash);
 for(int id:{IdDeletes,IdProtect,IdRecycle})
  SendMessageW(controls[id],BM_SETCHECK,BST_CHECKED,0);
 // Retain the established checkbox protocol for profile persistence and
 // existing Win32 automation. Its visual replacement is the mode selector.
 place(h,L"BUTTON",L"",BS_AUTOCHECKBOX,0,0,1,1,IdMirror);
 SendMessageW(controls[IdMirror],BM_SETCHECK,BST_UNCHECKED,0);

 place(h,L"STATIC",
  L"Run Preview to see planned transfers, deletions and conflicts.",
  0,0,0,340,30,IdPreviewInfo);
 place(h,L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|WS_HSCROLL|
  LBS_NOTIFY|WS_TABSTOP,0,0,400,180,IdChanges);
 button(h,L"Keep left",IdKeepLeft,0,0,115);
 button(h,L"Keep right",IdKeepRight,0,0,115);
 button(h,L"Skip file",IdSkip,0,0,115);

 place(h,L"STATIC",
  L"Restore a previous file version, inspect recovery archives, or open the run logs.\n"
  L"Existing history and backups remain on disk.",
  0,0,0,460,60,IdHistoryInfo);
 button(h,L"Browse recovery archives",IdOpenRecovery,0,0,270);
 button(h,L"Open execution logs",IdOpenLogs,0,0,270);

 button(h,L"Network & credentials",IdSettingsNetwork,0,0,205);
 button(h,L"Behavior & integration",IdSettingsBehavior,0,0,205);
 namedLabel(h,L"Source username",IdLabelLeftUser);
 namedLabel(h,L"Source password",IdLabelLeftPassword);
 edit(h,IdLeftUser,0,0,220);
 edit(h,IdLeftPassword,0,0,220,true);
 namedLabel(h,L"Destination username",IdLabelRightUser);
 namedLabel(h,L"Destination password",IdLabelRightPassword);
 edit(h,IdRightUser,0,0,220);
 edit(h,IdRightPassword,0,0,220,true);
 namedLabel(h,L"SFTP known_hosts file (required for host verification)",IdLabelHosts);
 edit(h,IdKnownHosts,0,0,440);
 button(h,L"Remember logins",IdRemember,0,0,185);
 button(h,L"Load saved logins",IdLoadCredentials,0,0,185);
 place(h,L"STATIC",
  L"Credentials are stored in Windows Credential Manager, never in profiles.",
  0,0,0,470,32,IdNetworkInfo);

 namedLabel(h,L"Conflict resolution",IdLabelPolicy);
 place(h,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,
  0,0,250,180,IdPolicy);
 for(const wchar_t* choice:{L"Ask / stop",L"Ignore conflicts",
  L"Stop on conflicts",L"Keep newest",L"Keep oldest"})
  SendMessageW(controls[IdPolicy],CB_ADDSTRING,0,
               reinterpret_cast<LPARAM>(choice));
 SendMessageW(controls[IdPolicy],CB_SETCURSEL,0,0);
 place(h,L"BUTTON",L"Start continuous mirror at Windows logon",
       BS_AUTOCHECKBOX|WS_TABSTOP,0,0,400,25,IdAutoStart);
 EnableWindow(controls[IdAutoStart],FALSE);
 place(h,L"BUTTON",L"Resume interrupted runs (rescan and reconcile)",
       BS_AUTOCHECKBOX|WS_TABSTOP,0,0,400,25,IdResume);
 place(h,L"BUTTON",L"Explorer context menu",
       BS_AUTOCHECKBOX|WS_TABSTOP,0,0,350,25,IdExplorer);
 SendMessageW(controls[IdExplorer],BM_SETCHECK,
  explorerMenuInstalled()?BST_CHECKED:BST_UNCHECKED,0);
 if(explorerMenuMachineInstalled()){
  SetWindowTextW(controls[IdExplorer],L"Explorer menu installed by MSI");
  EnableWindow(controls[IdExplorer],FALSE);
 }
 place(h,L"STATIC",L"Mirror startup may modify or delete target files.\n"
  L"Review Preview and keep recovery archives enabled.",
  0,0,0,440,43,IdBehaviorInfo);

 place(h,PROGRESS_CLASSW,L"",PBS_SMOOTH,0,0,210,8,IdProgress);
 place(h,L"STATIC",L"Ready",0,0,0,260,28,IdStatus);
 place(h,L"STATIC",L"Preview uses actual engine decisions, never estimates.\n"
  L"Use Preview before starting destructive operations.",
  0,0,0,208,56,IdSummaryHint);
 for(const auto& entry:{
  std::pair{IdSummaryTotal,L"No preview yet"},
  std::pair{IdSummaryCopies,L"Transfers  —"},
  std::pair{IdSummaryUpdates,L"Updates  —"},
  std::pair{IdSummaryDeletes,L"Deletions  —"},
  std::pair{IdSummaryConflicts,L"Conflicts  —"}})
  place(h,L"STATIC",entry.second,0,0,0,210,25,entry.first);
 updateModeDescription();
 reflowControls(h);
}
void updateModeDescription() {
 if(!controls[IdConfigurationHint]||!controls[IdMirror])return;
 const bool mirror=SendMessageW(controls[IdMirror],BM_GETCHECK,0,0)==BST_CHECKED;
 set(IdConfigurationHint,mirror
  ?L"Mirror keeps the source authoritative. Extra target files are removed.\n"
   L"Continuous monitoring runs until you press Stop."
  :L"Bidirectional synchronizes changes in both directions.\n"
   L"Preview shows conflicts before files are modified.");
}
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l){
 switch(message){
 case WM_CREATE:
  mainWindow=window;
  try{initializeControls(window);}catch(const std::exception& e){MessageBoxA(window,e.what(),"Initialization failed",MB_ICONERROR);return -1;}
  return 0;
 case WM_GETMINMAXINFO: {
  auto* limits=reinterpret_cast<MINMAXINFO*>(l);
  limits->ptMinTrackSize={890,540};
  return 0;
 }
 case WM_SIZE:
  if(controls[IdProfiles] && w!=SIZE_MINIMIZED)reflowControls(window);
  return 0;
 case WM_ERASEBKGND:return 1;
 case WM_PAINT: {
  PAINTSTRUCT paint{};
  const HDC dc=BeginPaint(window,&paint);
  const wchar_t* title=activeTab==MainTab::Configuration?L"Sync configuration":
   activeTab==MainTab::Preview?L"Preview & conflicts":
   activeTab==MainTab::History?L"History & recovery":L"Settings";
  synchrorep::winui::ui_paint_frame(dc,mainLayout,title,busy,progressFraction);
  for(int id:{IdName,IdLeft,IdRight,IdLeftUser,IdLeftPassword,
              IdRightUser,IdRightPassword,IdKnownHosts}) {
   if(controls[id] && IsWindowVisible(controls[id])) {
    RECT frame{};
    GetWindowRect(controls[id],&frame);
    MapWindowPoints(HWND_DESKTOP,window,reinterpret_cast<POINT*>(&frame),2);
    InflateRect(&frame,3,3);
    synchrorep::winui::ui_paint_edit_frame(dc,frame);
   }
  }
  EndPaint(window,&paint);
  return 0;
 }
 case WM_MEASUREITEM: {
  auto* item=reinterpret_cast<MEASUREITEMSTRUCT*>(l);
  if(item->CtlID==IdProfiles){item->itemHeight=50;return TRUE;}
  return FALSE;
 }
 case WM_DRAWITEM: {
  const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);
  if(item->CtlID==IdProfiles && item->CtlType==ODT_LISTBOX){
   const auto index=item->itemID;
   const std::wstring name=index<profiles.size()?
    toWide(profiles[index].name):L"";
   synchrorep::winui::ui_draw_profile(*item,name.c_str());
   return TRUE;
  }
  if(item->CtlType==ODT_BUTTON) {
   const auto id=static_cast<int>(item->CtlID);
   const bool selected=(id==IdTabConfiguration && activeTab==MainTab::Configuration)||
    (id==IdTabPreview && activeTab==MainTab::Preview)||
    (id==IdTabHistory && activeTab==MainTab::History)||
    (id==IdTabSettings && activeTab==MainTab::Settings)||
    (id==IdSettingsNetwork && settingsPane==SettingsPane::Network)||
    (id==IdSettingsBehavior && settingsPane==SettingsPane::Behavior)||
    (id==IdModeBidirectional &&
     SendMessageW(controls[IdMirror],BM_GETCHECK,0,0)!=BST_CHECKED)||
    (id==IdModeMirror &&
     SendMessageW(controls[IdMirror],BM_GETCHECK,0,0)==BST_CHECKED);
   synchrorep::winui::ui_draw_button(*item,selected,id==IdExecute,
                                     id==IdDelete||id==IdCancel);
   return TRUE;
  }
  return FALSE;
 }
 case WM_CTLCOLOREDIT:
 case WM_CTLCOLORLISTBOX: {
  const HDC dc=reinterpret_cast<HDC>(w);
  SetTextColor(dc,synchrorep::winui::color::white);
  SetBkColor(dc,synchrorep::winui::color::input);
  return reinterpret_cast<LRESULT>(synchrorep::winui::ui_input_brush());
 }
 case WM_CTLCOLORSTATIC:
 case WM_CTLCOLORBTN: {
  const HDC dc=reinterpret_cast<HDC>(w);
  SetTextColor(dc,synchrorep::winui::color::white);
  SetBkColor(dc,synchrorep::winui::color::card);
  return reinterpret_cast<LRESULT>(synchrorep::winui::ui_card_brush());
 }
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
  if(busy){
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
  const auto event=LOWORD(l);
  if(event==WM_LBUTTONDBLCLK||event==NIN_SELECT||event==NIN_KEYSELECT){
   showMainWindow();return 0;
  }
  if(event==WM_RBUTTONUP||event==WM_CONTEXTMENU){
   HMENU menu=CreatePopupMenu();
   if(!menu)return 0;
   AppendMenuW(menu,MF_STRING,901,L"Show Synchrorep");
   AppendMenuW(menu,MF_STRING|(busy&&activeMirror?0:MF_GRAYED),
               902,L"Stop continuous mirror");
   AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
   AppendMenuW(menu,MF_STRING,903,L"Exit Synchrorep");
   POINT cursor{};GetCursorPos(&cursor);
   SetForegroundWindow(window);
   const auto action=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,
                                    cursor.x,cursor.y,0,window,nullptr);
   DestroyMenu(menu);
   PostMessageW(window,WM_NULL,0,0);
   if(action==901)showMainWindow();
   if(action==902&&job)job->stop();
   if(action==903)DestroyWindow(window);
   return 0;
  }
  return 0;
 }
 case WM_DESTROY: {
  shuttingDown=true;
  if(syncWindow)DestroyWindow(syncWindow);
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
  synchrorep::winui::ui_cleanup();
  PostQuitMessage(0);
  return 0;
 }
 default:return DefWindowProcW(window,message,w,l);
 }
}
// Explorer's "Synchronize with Synchrorep" command is an action, not
// a shortcut to the settings window. The original GNOME app selects a
// second folder and immediately executes the new configuration.
std::optional<std::wstring> chooseDestinationFolder(HWND owner,
                                                     const std::wstring& source){
 IFileOpenDialog* native=nullptr;
 const auto hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&native));
 if(FAILED(hr))throw std::runtime_error("Cannot open Windows destination folder picker");
 const std::unique_ptr<IFileOpenDialog,void(*)(IFileOpenDialog*)> dialog(
  native,[](IFileOpenDialog* ptr){if(ptr)ptr->Release();});
 DWORD options=0;
 if(FAILED(dialog->GetOptions(&options)) ||
    FAILED(dialog->SetOptions(options|FOS_PICKFOLDERS|
                             FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR)))
  throw std::runtime_error("Cannot configure destination folder picker");
 // The Windows Shell exposes WSL distributions under the Linux namespace.
 // Keep the native dialog and add its UNC location when Windows resolves it.
 IShellItem* linuxRoot=nullptr;
 if(SUCCEEDED(SHCreateItemFromParsingName(L"\\\\wsl.localhost",nullptr,
                                        IID_PPV_ARGS(&linuxRoot)))){
  dialog->AddPlace(linuxRoot,FDAP_TOP);
  linuxRoot->Release();
 }
 const auto title=L"Choose the folder to synchronize with "+source;
 dialog->SetTitle(title.c_str());
 dialog->SetOkButtonLabel(L"Synchronize");
 const auto showResult=dialog->Show(owner);
 if(showResult==HRESULT_FROM_WIN32(ERROR_CANCELLED))return std::nullopt;
 if(FAILED(showResult))throw std::runtime_error("Could not choose destination folder");
 IShellItem* selected=nullptr;
 if(FAILED(dialog->GetResult(&selected)))
  throw std::runtime_error("Cannot resolve destination folder");
 const std::unique_ptr<IShellItem,void(*)(IShellItem*)> item(
  selected,[](IShellItem* ptr){if(ptr)ptr->Release();});
 PWSTR path=nullptr;
 if(FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))||!path)
  throw std::runtime_error("Destination must be a local or network filesystem folder");
 std::wstring destination(path);
 CoTaskMemFree(path);
 return destination;
}
void beginExplorerSynchronization(HWND window,const std::wstring& sourcePath){
 const std::filesystem::path source(sourcePath);
 if(!std::filesystem::is_directory(source))
  throw std::runtime_error("Selected Explorer source must be an existing directory");
 const auto sourceUtf8=toUtf8(sourcePath);
 int found=explorer_profile_index(profiles,sourceUtf8);
 if(found>=0){
  loadProfile(found);
  // Restore saved credentials before starting an existing FTP/SFTP profile.
  // This is needed for Explorer's one-click execution path.
  (void)loadCredential(IdLeft,IdLeftUser,IdLeftPassword);
  (void)loadCredential(IdRight,IdRightUser,IdRightPassword);
  showMainWindow();
  startSync(true);
  return;
 }
 // No saved configuration: offer a native Windows folder selection dialog.
 // Cancellation MUST NOT create a profile or launch an operation.
 const auto destination=chooseDestinationFolder(window,sourcePath);
 if(!destination){
  DestroyWindow(window);
  return;
 }
 const std::filesystem::path target(*destination);
 if(!std::filesystem::is_directory(target))
  throw std::runtime_error("Selected destination must be an existing directory");
 // Resolve junctions/reparse points so two nested aliases cannot start
 // a recursive synchronization, even if lexical paths look different.
 const auto canonicalSource=canonical_endpoint_path(source);
 const auto canonicalTarget=canonical_endpoint_path(target);
 if(mirror_targets_overlap(toUtf8(canonicalSource.wstring()),
                            toUtf8(canonicalTarget.wstring())))
  throw std::runtime_error("Source and destination must be different, non-nested folders");
 Profile profile=explorer_new_profile(profiles,sourceUtf8,toUtf8(*destination));
 // Persist defaults BEFORE the first synchronization so it can be resumed.
 // This also guarantees that the next Explorer invocation reuses the profile.
 auto saved=profiles;
 saved.push_back(profile);
 ProfileStore(appDirectory()/"profiles.dat").save(saved);
 profiles=std::move(saved);
 loadProfile(static_cast<int>(profiles.size()-1));
 showMainWindow();
 startSync(true);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
 // Headless check of the ACTUAL executable and runtime DLLs.
 // Windows' loader fails before WinMain if a dependent DLL is missing.
 int argumentCount=0;
 LPWSTR* arguments=CommandLineToArgvW(GetCommandLineW(),&argumentCount);
 bool checkRuntime=arguments&&argumentCount==2&&
    std::wcscmp(arguments[1],L"--check-runtime")==0;
 if(arguments)LocalFree(arguments);
 if(checkRuntime){
  const auto* version=curl_version_info(CURLVERSION_NOW);
  if(!version||!version->protocols||(version->features&CURL_VERSION_SSL)==0)return 2;
  bool ftp=false,sftp=false;
  for(auto protocol=version->protocols;*protocol;++protocol){
   ftp|=std::strcmp(*protocol,"ftp")==0;
   sftp|=std::strcmp(*protocol,"sftp")==0;
  }
  return ftp&&sftp?0:3;
 }
 const HRESULT comStatus=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 if(FAILED(comStatus))return 4;
 WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=instance;
 wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));
 wc.lpszClassName=L"SynchrorepWindow";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
 wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
 INITCOMMONCONTROLSEX init{sizeof(init),ICC_PROGRESS_CLASS};
 InitCommonControlsEx(&init);
 RegisterClassW(&wc);
 const wchar_t* title=L"Synchrorep";
 RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
 const int availableW=work.right-work.left;
 const int availableH=work.bottom-work.top;
 const int width=std::min(1160,std::max(820,availableW-32));
 const int height=std::min(720,std::max(510,availableH-65));
 constexpr DWORD style=WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;
 RECT bounds{0,0,width,height};
 AdjustWindowRectEx(&bounds,style,FALSE,0);
 const int outerWidth=bounds.right-bounds.left;
 const int outerHeight=bounds.bottom-bounds.top;
 HWND window=CreateWindowW(wc.lpszClassName,title,style,
   work.left+std::max(0,(availableW-outerWidth)/2),
   work.top+std::max(0,(availableH-outerHeight)/2),
   outerWidth,outerHeight,nullptr,nullptr,instance,nullptr);
 if(!window){CoUninitialize();return 1;}
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
   // Update Run paths if this executable was moved or upgraded.
   wchar_t module[32768]{};
   const auto length=GetModuleFileNameW(nullptr,module,32768);
   if(length>0&&length<32768 &&
      GetEnvironmentVariableW(L"SYNCHROREP_UI_SMOKE",nullptr,0)==0)
    synchrorep::winui::synchronize_startup(profiles,module);
   if(!folder.empty()){
    beginExplorerSynchronization(window,folder);
   }else{
    if(!profiles.empty())loadProfile(0);
    else{
     set(IdName,L"New synchronization");
     wchar_t user[32768]{};
     DWORD n=GetEnvironmentVariableW(L"USERPROFILE",user,32768);
     if(n>0&&n<32768)set(IdKnownHosts,std::wstring(user)+L"\\.ssh\\known_hosts");
    }
    ShowWindow(window,show);
   }
  }
 }catch(const std::exception& e){
  ShowWindow(window,SW_SHOWNORMAL);
  MessageBoxA(window,e.what(),"Synchrorep startup error",MB_ICONERROR);
 }
 MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
 CoUninitialize();
 return 0;
}
