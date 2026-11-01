// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdlib>

namespace {
namespace fs=std::filesystem;
constexpr int IdProfiles=101,IdSave=103,IdName=105,IdMirror=132;
constexpr int IdLeft=106,IdRight=107,IdPreview=117,IdChanges=119,IdStatus=126;
constexpr int IdLeftUser=108,IdPolicy=115,IdOpenRecovery=129;
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
LRESULT safeMessage(HWND target,UINT message,WPARAM w,LPARAM l,
                    const char* operation) {
 DWORD_PTR response=0;
 SetLastError(ERROR_SUCCESS);
 const LRESULT sent=SendMessageTimeoutW(
  target,message,w,l,SMTO_ABORTIFHUNG|SMTO_ERRORONEXIT,3000,&response);
 if(!sent)
  throw std::runtime_error(std::string("GUI message timed out: ")+operation+
    " (Windows error "+std::to_string(GetLastError())+")");
 return static_cast<LRESULT>(response);
}
void click(HWND window,int id) {
 HWND control=GetDlgItem(window,id);
 require(control!=nullptr,"UI button is missing");
 safeMessage(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),
             reinterpret_cast<LPARAM>(control),
             ("button "+std::to_string(id)).c_str());
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
 if(!(bounds.left>=0 && bounds.top>=0 &&
      bounds.right<=client.right && bounds.bottom<=client.bottom)) {
  throw std::runtime_error(
    "Visible control "+std::to_string(id)+
    " outside compact client: bounds=["+
    std::to_string(bounds.left)+","+
    std::to_string(bounds.top)+","+
    std::to_string(bounds.right)+","+
    std::to_string(bounds.bottom)+
    "], client=["+std::to_string(client.right)+","+
    std::to_string(client.bottom)+"]");
 }
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

  STARTUPINFOW startup{};
  startup.cb=sizeof(startup);
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
  std::cerr<<"UI smoke: main window created\n";
  require(SetWindowPos(app,nullptr,0,0,990,610,
    SWP_NOMOVE|SWP_NOZORDER|SWP_SHOWWINDOW|SWP_ASYNCWINDOWPOS)!=FALSE,
    "Cannot resize isolated UI");
  // SWP_ASYNCWINDOWPOS schedules the resize on the GUI thread, which may
  // still be initializing on a loaded Windows 11 desktop. Poll until the
  // requested size AND the child layout settle rather than assuming 300 ms.
  // Keep a finite deadline and preserve exact geometry on a real defect.
  bool layoutReady=false;
  std::string layoutFailure="window resize not processed";
  for(int attempt=0;attempt<100;++attempt) {
   if(WaitForSingleObject(process.hProcess,0)!=WAIT_TIMEOUT)
    throw std::runtime_error("GUI exited during compact resize");
   RECT frame{};
   if(!GetWindowRect(app,&frame))
    throw std::runtime_error("Cannot read resized window rectangle");
   const int frameWidth=frame.right-frame.left;
   const int frameHeight=frame.bottom-frame.top;
   if(frameWidth!=990 || frameHeight!=610) {
    layoutFailure="requested 990x610, current outer window is "+
      std::to_string(frameWidth)+"x"+std::to_string(frameHeight);
    Sleep(50);
    continue;
   }
   try {
    visible(app,IdProfiles,true);
    visible(app,IdName,true);
    visible(app,IdSave,true);
    visible(app,IdChanges,false);
    visible(app,IdOpenRecovery,false);
    layoutReady=true;
    break;
   }catch(const std::exception& ex) {
    layoutFailure=ex.what();
    Sleep(50);
   }
  }
  if(!layoutReady)
   throw std::runtime_error("Compact layout did not settle: "+layoutFailure);

  std::cerr<<"UI smoke: testing navigation and sync mode\n";
  click(app,IdModeMirror);
  require(safeMessage(GetDlgItem(app,IdMirror),BM_GETCHECK,0,0,
                      "mirror state")==BST_CHECKED,
          "Mirror segment did not set persisted mirror mode");
  click(app,IdModeBidirectional);
  require(safeMessage(GetDlgItem(app,IdMirror),BM_GETCHECK,0,0,
                      "bidirectional state")==BST_UNCHECKED,
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

  std::cerr<<"UI smoke: navigation passed\n";
  const bool interactivePreview=
   GetEnvironmentVariableW(L"SYNCHROREP_INTERACTIVE_UI_PREVIEW",nullptr,0)>0;
  if(interactivePreview) {
  std::cerr<<"UI smoke: starting interactive preview\n";
  // Exercise a real preview through the asynchronous GUI pipeline, without
  // mutating either source or destination. It also opens the themed progress
  // window and transitions to the Preview tab automatically on completion.
  fs::create_directories(isolated/"source");
  fs::create_directories(isolated/"target");
  std::ofstream(isolated/"source"/"readme.txt")<<"UI preview file";
  safeMessage(GetDlgItem(app,IdLeft),WM_SETTEXT,0,
    reinterpret_cast<LPARAM>((isolated/"source").c_str()),"source input");
  safeMessage(GetDlgItem(app,IdRight),WM_SETTEXT,0,
    reinterpret_cast<LPARAM>((isolated/"target").c_str()),"destination input");
  click(app,IdPreview);
  bool previewComplete=false;
  for(int attempt=0;attempt<400;++attempt) {
   if(WaitForSingleObject(process.hProcess,0)!=WAIT_TIMEOUT)
    throw std::runtime_error("GUI exited while preparing the preview");
   wchar_t status[256]{};
   GetWindowTextW(GetDlgItem(app,IdStatus),status,256);
   if(std::wstring(status).find(L"Preview complete")!=std::wstring::npos) {
    previewComplete=true;
    break;
   }
   if(std::wstring(status).find(L"Operation failed")!=std::wstring::npos)
    throw std::runtime_error("Asynchronous GUI preview failed");
   Sleep(50);
  }
  require(previewComplete,"Asynchronous preview did not complete");
  visible(app,IdChanges,true);
  require(safeMessage(GetDlgItem(app,IdChanges),LB_GETCOUNT,0,0,
                      "preview operation count")>=1,
          "Preview did not list the source file");
  require(!fs::exists(isolated/"target"/"readme.txt"),
          "Preview incorrectly modified the destination");
  std::cerr<<"UI smoke: preview passed\n";
  }else {
   std::cerr<<"UI smoke: interactive preview reserved for Wine/Windows desktop\n";
  }
  std::cerr<<"UI smoke: closing GUI\n";
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
