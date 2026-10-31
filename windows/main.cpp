#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include "synchrorep/engine.hpp"
static HWND leftField,rightField;
static std::wstring getText(HWND h) {
 int n=GetWindowTextLengthW(h);
 std::wstring value(static_cast<std::size_t>(n+1),L'\0');
 GetWindowTextW(h,value.data(),n+1);
 value.resize(n);return value;
}
static std::string utf8(const std::wstring& value) {
 if(value.empty())return {};
 int n=WideCharToMultiByte(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
 std::string out(static_cast<std::size_t>(n),'\0');
 WideCharToMultiByte(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr);
 return out;
}
static std::filesystem::path statePath(const std::string& left,const std::string& right) {
 // A separate historical snapshot for each pair is essential for deletions.
 wchar_t buffer[32768]{};
 DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,32768);
 std::filesystem::path dir=(n>0&&n<32768)?std::filesystem::path(buffer):std::filesystem::temp_directory_path();
 std::uint64_t h=14695981039346656037ull;
 for(unsigned char c:left+"\n"+right){h^=c;h*=1099511628211ull;}
 std::wostringstream hex;hex<<std::hex<<h;
 return dir/L"Synchrorep"/(hex.str()+L".state");
}
static void execute(HWND hwnd,bool preview) {
 try {
  const auto left=utf8(getText(leftField)),right=utf8(getText(rightField));
  auto a=synchrorep::open_endpoint(left),b=synchrorep::open_endpoint(right);
  synchrorep::Engine engine(statePath(a->identity(),b->identity()));
  const auto plan=engine.plan(*a,*b);
  std::size_t copies=0,removes=0,conflicts=0;
  for(auto& d:plan){
   switch(d.action){
   case synchrorep::Action::CopyLeftToRight:
   case synchrorep::Action::CopyRightToLeft:++copies;break;
   case synchrorep::Action::DeleteLeft:
   case synchrorep::Action::DeleteRight:++removes;break;
   case synchrorep::Action::Conflict:++conflicts;break;
   default:break;
   }
  }
  if(preview){
   std::wstring label=L"Copies: "+std::to_wstring(copies)+L"\nDeletions: "+
      std::to_wstring(removes)+L"\nConflicts: "+std::to_wstring(conflicts);
   MessageBoxW(hwnd,label.c_str(),L"Synchrorep preview",MB_ICONINFORMATION);
   return;
  }
  if(removes>0&&MessageBoxW(hwnd,L"Synchronization includes deletions. Continue?",
    L"Synchrorep",MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2)!=IDYES)return;
  auto result=engine.run(*a,*b);
  std::wstring text=L"Copied: "+std::to_wstring(result.copied)+L"\nRemoved: "+
    std::to_wstring(result.removed)+L"\nConflicts: "+std::to_wstring(result.conflicts.size());
  MessageBoxW(hwnd,text.c_str(),L"Synchrorep",result.conflicts.empty()?MB_ICONINFORMATION:MB_ICONWARNING);
 }catch(const std::exception& ex){MessageBoxA(hwnd,ex.what(),"Synchrorep error",MB_ICONERROR);}
}
static LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_CREATE){
  CreateWindowW(L"STATIC",L"Left directory (local or wsl://):",WS_CHILD|WS_VISIBLE,16,20,400,24,h,nullptr,nullptr,nullptr);
  leftField=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,16,45,460,27,h,(HMENU)1,nullptr,nullptr);
  CreateWindowW(L"STATIC",L"Right directory:",WS_CHILD|WS_VISIBLE,16,88,350,24,h,nullptr,nullptr,nullptr);
  rightField=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,16,113,460,27,h,(HMENU)2,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Preview",WS_CHILD|WS_VISIBLE|WS_TABSTOP,16,164,150,34,h,(HMENU)4,nullptr,nullptr);
  CreateWindowW(L"BUTTON",L"Synchronize",WS_CHILD|WS_VISIBLE|WS_TABSTOP,175,164,150,34,h,(HMENU)3,nullptr,nullptr);
  return 0;
 }
 if(m==WM_COMMAND&&(LOWORD(w)==3||LOWORD(w)==4)){execute(h,LOWORD(w)==4);return 0;}
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
 return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
 WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=instance;
 wc.lpszClassName=L"SynchrorepWindow";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
 RegisterClassW(&wc);
 HWND h=CreateWindowW(wc.lpszClassName,L"Synchrorep",WS_OVERLAPPEDWINDOW,
 CW_USEDEFAULT,CW_USEDEFAULT,520,280,nullptr,nullptr,instance,nullptr);
 int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
 if(argv){if(argc>1)SetWindowTextW(leftField,argv[1]);LocalFree(argv);}
 ShowWindow(h,show);MSG msg;
 while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
 return 0;
}
