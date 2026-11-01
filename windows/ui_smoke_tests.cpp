#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdlib>

namespace {
namespace fs=std::filesystem;
constexpr int IdProfiles=101,IdSave=103,IdName=105,IdMirror=132;
constexpr int IdLeftUser=108,IdChanges=119,IdPolicy=115,IdOpenRecovery=129;
constexpr int IdTabConfiguration=200,IdTabPreview=201,IdTabHistory=202,
 IdTabSettings=203,IdModeBidirectional=206,IdModeMirror=207,
 IdSettingsNetwork=208,IdSettingsBehavior=209;
void require(bool valid,const char* message) {
 if(!valid)throw std::runtime_error(message);
}
struct Finder { DWORD pid; HWND found=nullptr; };
BOOL CALLBACK findWindow(HWND hwnd,LPARAM argument) {
 auto* state=reinterpret_cast<Finder*>(argument);
 DWORD pid=0;
 GetWindowThreadProcessId(hwnd,&pid);
 if(pid!=state->pid)return TRUE;
 wchar_t cls[96]{};
 GetClassNameW(hwnd,cls,96);
 if(std::wstring(cls)==L"SynchrorepWindow") {
  state->found=hwnd;
  return FALSE;
 }
 return TRUE;
}
HWND findApp(DWORD pid) {
 Finder state{pid};
 EnumWindows(findWindow,reinterpret_cast<LPARAM>(&state));
 return state.found;
}
void click(HWND window,int id) {
 HWND control=GetDlgItem(window,id);
 require(control!=nullptr,"UI button is missing");
 SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),
              reinterpret_cast<LPARAM>(control));
}
void visible(HWND window,int id,bool expected) {
 HWND child=GetDlgItem(window,id);
 require(child!=nullptr,"Control handle is missing");
 require((IsWindowVisible(child)!=FALSE)==expected,
         "Control is on the wrong tab");
 if(!expected)return;
 RECT bounds{},client{};
 GetWindowRect(child,&bounds);
 MapWindowPoints(HWND_DESKTOP,window,reinterpret_cast<POINT*>(&bounds),2);
 GetClientRect(window,&client);
 require(bounds.left>=0 && bounds.top>=0 &&
         bounds.right<=client.right && bounds.bottom<=client.bottom,
         "Visible control does not fit the compact client area");
}
}

int main() {
 PROCESS_INFORMATION process{};
 fs::path isolated;
 try {
  wchar_t module[32768]{};
  require(GetModuleFileNameW(nullptr,module,32768)>0,"Module path missing");
  fs::path executable=fs::path(module).parent_path()/L"synchrorep.exe";
  require(fs::exists(executable),"Synchrorep GUI executable is missing");

  isolated=fs::temp_directory_path()/
    (L"synchrorep-ui-smoke-"+std::to_wstring(GetCurrentProcessId()));
  fs::create_directories(isolated);
  const DWORD needed=GetEnvironmentVariableW(L"LOCALAPPDATA",nullptr,0);
  std::wstring previous(needed?needed:1,L'\0');
  if(needed)GetEnvironmentVariableW(L"LOCALAPPDATA",previous.data(),needed);
  const bool hadPrevious=needed>0;
  if(hadPrevious)previous.resize(needed-1);
  SetEnvironmentVariableW(L"LOCALAPPDATA",isolated.c_str());
  SetEnvironmentVariableW(L"SYNCHROREP_UI_SMOKE",L"1");

  STARTUPINFOW startup{sizeof(startup)};
  std::wstring command=L"\""+executable.wstring()+L"\"";
  const BOOL started=CreateProcessW(nullptr,command.data(),nullptr,nullptr,
                    FALSE,0,nullptr,executable.parent_path().c_str(),
                    &startup,&process);
  SetEnvironmentVariableW(L"SYNCHROREP_UI_SMOKE",nullptr);
  SetEnvironmentVariableW(L"LOCALAPPDATA",
                          hadPrevious?previous.c_str():nullptr);
  require(started!=FALSE,"Cannot start isolated Synchrorep GUI");

  HWND app=nullptr;
  for(int attempt=0;attempt<200;++attempt) {
   app=findApp(process.dwProcessId);
   if(app)break;
   if(WaitForSingleObject(process.hProcess,0)!=WAIT_TIMEOUT)
    throw std::runtime_error("GUI exited before creating its main window");
   Sleep(50);
  }
  require(app!=nullptr,"Synchrorep main window did not appear");
  SetWindowPos(app,nullptr,0,0,990,610,SWP_NOMOVE|SWP_NOZORDER|SWP_SHOWWINDOW);
  Sleep(200);

  visible(app,IdProfiles,true);
  visible(app,IdName,true);
  visible(app,IdSave,true);
  visible(app,IdChanges,false);
  visible(app,IdOpenRecovery,false);

  click(app,IdModeMirror);
  require(SendMessageW(GetDlgItem(app,IdMirror),BM_GETCHECK,0,0)==BST_CHECKED,
          "Mirror segment did not set persisted mirror mode");
  click(app,IdModeBidirectional);
  require(SendMessageW(GetDlgItem(app,IdMirror),BM_GETCHECK,0,0)==BST_UNCHECKED,
          "Bidirectional segment did not restore default mode");

  click(app,IdTabPreview);
  visible(app,IdChanges,true);
  visible(app,IdName,false);
  visible(app,IdSave,true);

  click(app,IdTabHistory);
  visible(app,IdOpenRecovery,true);
  visible(app,IdChanges,false);

  click(app,IdTabSettings);
  visible(app,IdLeftUser,true);
  visible(app,IdName,false);
  click(app,IdSettingsBehavior);
  visible(app,IdPolicy,true);
  visible(app,IdLeftUser,false);
  click(app,IdSettingsNetwork);
  visible(app,IdLeftUser,true);
  visible(app,IdPolicy,false);

  click(app,IdTabConfiguration);
  visible(app,IdName,true);
  visible(app,IdChanges,false);
  require(!fs::exists(isolated/"Synchrorep"/"profiles.dat"),
          "Tab navigation unexpectedly created a synchronization profile");
  PostMessageW(app,WM_CLOSE,0,0);
  require(WaitForSingleObject(process.hProcess,7000)==WAIT_OBJECT_0,
          "Closing the GUI did not terminate the process");
  std::cout<<"Windows native 12-inch UI smoke test passed\n";
 }catch(const std::exception& error) {
  std::cerr<<"Windows native UI smoke test FAILED: "<<error.what()<<'\n';
  if(process.hProcess)TerminateProcess(process.hProcess,1);
  if(process.hProcess)WaitForSingleObject(process.hProcess,3000);
  if(process.hThread)CloseHandle(process.hThread);
  if(process.hProcess)CloseHandle(process.hProcess);
  if(!isolated.empty()) {std::error_code ec;fs::remove_all(isolated,ec);}
  return 1;
 }
 if(process.hThread)CloseHandle(process.hThread);
 if(process.hProcess)CloseHandle(process.hProcess);
 if(!isolated.empty()) {std::error_code ec;fs::remove_all(isolated,ec);}
 return 0;
}
