#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "startup_registry.hpp"
#include "synchrorep/startup_profiles.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>
#include <filesystem>
#include <iostream>
using namespace synchrorep;

namespace {
std::wstring to_wide(const std::string& s){return {s.begin(),s.end()};}
std::wstring value(HKEY key,const std::wstring& name){
 wchar_t buffer[4096]{};
 DWORD bytes=sizeof(buffer),type=0;
 const auto status=RegQueryValueExW(key,name.c_str(),nullptr,&type,
                                   reinterpret_cast<BYTE*>(buffer),&bytes);
 assert(status==ERROR_SUCCESS && type==REG_SZ);
 return buffer;
}
bool exists(HKEY key,const std::wstring& name){
 return RegQueryValueExW(key,name.c_str(),nullptr,nullptr,nullptr,nullptr)==ERROR_SUCCESS;
}
}
int main(){
 wchar_t executable[32768]{};
 const DWORD size=GetModuleFileNameW(nullptr,executable,32768);
 assert(size>0&&size<32768);
 const auto temp=std::wstring(L"Software\\Synchrorep\\Tests\\Startup-")+
                  std::to_wstring(GetCurrentProcessId());
 HKEY key=nullptr;
 assert(RegCreateKeyExW(HKEY_CURRENT_USER,temp.c_str(),0,nullptr,0,
       KEY_ALL_ACCESS,nullptr,&key,nullptr)==ERROR_SUCCESS);
 try{
  const wchar_t* sentinel=L"keep me";
  assert(RegSetValueExW(key,L"OtherApp",0,REG_SZ,reinterpret_cast<const BYTE*>(sentinel),
         static_cast<DWORD>((wcslen(sentinel)+1)*sizeof(wchar_t)))==ERROR_SUCCESS);
  Profile a;
  a.name="First profile";
  a.left="C:/local-a";
  a.right="sftp://host/a";
  a.mode=SyncMode::Mirror;
  a.auto_start=true;
  Profile b=a;
  b.name="Second profile";
  b.left="C:/local-b";
  const auto entries=startup_entries({a,b});
  assert(entries.size()==2);
  winui::synchronize_startup({a,b},executable,temp);
  // The same destination must resolve to the SAME cross-process mutex,
  // even when profiles use different separators or drive letter case.
  const auto mutexName=mirror_target_mutex("C:/shared/mirror");
  const std::wstring mutexWide(mutexName.begin(),mutexName.end());
  HANDLE first=CreateMutexW(nullptr,FALSE,mutexWide.c_str());
  assert(first!=nullptr);
  assert(GetLastError()!=ERROR_ALREADY_EXISTS);
  const auto alias=mirror_target_mutex("c:\\\\SHARED\\\\MIRROR\\\\");
  assert(alias==mutexName);
  HANDLE second=CreateMutexW(nullptr,FALSE,mutexWide.c_str());
  assert(second!=nullptr && GetLastError()==ERROR_ALREADY_EXISTS);
  CloseHandle(second);
  CloseHandle(first);
  for(const auto& entry:entries){
   const auto name=to_wide(entry.value_name);
   assert(exists(key,name));
   const auto command=value(key,name);
   assert(command.find(L" --auto-mirror ")!=std::wstring::npos);
   assert(command.find(to_wide(entry.profile_name))!=std::wstring::npos);
   assert(command.find(L"secret")==std::wstring::npos);
  }
  assert(value(key,L"OtherApp")==sentinel);
  b.auto_start=false;
  winui::synchronize_startup({a,b},executable,temp);
  assert(exists(key,to_wide(entries[0].value_name)));
  assert(!exists(key,to_wide(entries[1].value_name)));
  winui::synchronize_startup({},executable,temp);
  assert(!exists(key,to_wide(entries[0].value_name)));
  assert(value(key,L"OtherApp")==sentinel);
 }catch(...){
  RegCloseKey(key);
  RegDeleteTreeW(HKEY_CURRENT_USER,temp.c_str());
  throw;
 }
 RegCloseKey(key);
 assert(RegDeleteTreeW(HKEY_CURRENT_USER,temp.c_str())==ERROR_SUCCESS);
 std::cout<<"HKCU startup registry integration passed\n";
}
