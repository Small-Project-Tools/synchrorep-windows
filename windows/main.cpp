#define UNICODE
#define _UNICODE
#include <windows.h>
#include <string>
#include <filesystem>
#include "synchrorep/engine.hpp"
static HWND leftField,rightField;
static std::wstring getText(HWND h){int n=GetWindowTextLengthW(h);std::wstring v(n,L'\0');GetWindowTextW(h,v.data(),n+1);return v;}
static std::wstring statePath(){
 wchar_t* appdata=nullptr;
 size_t len=0;
 _wdupenv_s(&appdata,&len,L"LOCALAPPDATA");
 std::filesystem::path p=appdata?appdata:L".";
 free(appdata);return (p/L"Synchrorep"/L"history.txt").wstring();
}
static LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_CREATE){
  CreateWindowW(L"STATIC",L"Left directory:",WS_CHILD|WS_VISIBLE,16,20,350,24,h,nullptr,nullptr,nullptr);
  leftField=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,16,45,460,27,h,(HMENU)1,nullptr,nullptr);
  CreateWindowW(L"STATIC",L"Right directory:",WS_CHILD|WS_VISIBLE,16,88,350,24,h,nullptr,nullptr,nullptr);
  rightField=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,16,113,460,27,h,(HMENU)2,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Synchronize",WS_CHILD|WS_VISIBLE|WS_TABSTOP,16,164,150,34,h,(HMENU)3,nullptr,nullptr);
  return 0;
 }
 if(m==WM_COMMAND&&LOWORD(w)==3){
  try{
   synchrorep::Engine e(statePath());auto r=e.run(getText(leftField),getText(rightField));
   std::wstring s=L"Copied: "+std::to_wstring(r.copied)+L"\nRemoved: "+std::to_wstring(r.removed)+L"\nConflicts: "+std::to_wstring(r.conflicts.size());
   MessageBoxW(h,s.c_str(),r.conflicts.empty()?L"Synchrorep":L"Synchronization conflicts",r.conflicts.empty()?MB_OK|MB_ICONINFORMATION:MB_OK|MB_ICONWARNING);
  }catch(const std::exception& ex){MessageBoxA(h,ex.what(),"Synchrorep error",MB_ICONERROR);}
  return 0;
 }
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
 return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR cmd,int show){
 WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.lpszClassName=L"SynchrorepWindow";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
 RegisterClassW(&wc);
 HWND h=CreateWindowW(wc.lpszClassName,L"Synchrorep",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,520,280,nullptr,nullptr,instance,nullptr);
 if(cmd&&*cmd){SetWindowTextW(leftField,cmd);}
 ShowWindow(h,show);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
}
