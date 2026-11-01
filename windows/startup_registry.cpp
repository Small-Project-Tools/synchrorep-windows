// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "startup_registry.hpp"
#include "windows_args.hpp"
#include "synchrorep/startup_profiles.hpp"
#include <string>
#include <vector>
#include <map>
#include <stdexcept>

namespace synchrorep::winui {
namespace {
constexpr wchar_t kPrefix[]=L"SynchrorepMirror-";
std::wstring wide(const std::string& text){
 if(text.empty())return {};
 const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
 if(!count)throw std::runtime_error("Invalid profile name (UTF-8)");
 std::wstring output(static_cast<std::size_t>(count),L'\0');
 if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),output.data(),count))
  throw std::runtime_error("Cannot convert profile name");
 return output;
}
void check(LSTATUS result,const char* action){
 if(result!=ERROR_SUCCESS)
  throw std::runtime_error(std::string(action)+": Windows error "+std::to_string(result));
}
struct Key{
 HKEY handle=nullptr;
 ~Key(){if(handle)RegCloseKey(handle);}
 Key(const Key&)=delete;
 Key& operator=(const Key&)=delete;
 Key()=default;
};
}
void synchronize_startup(const std::vector<Profile>& profiles,const std::filesystem::path& executable,
                         const std::wstring& registry_path){
 const auto entries=startup_entries(profiles);
 Key run;
 // Avoid creating an empty Run key if the user has disabled autostart.
 if(entries.empty()){
  const auto status=RegOpenKeyExW(HKEY_CURRENT_USER,registry_path.c_str(),0,KEY_READ|KEY_WRITE,&run.handle);
  if(status==ERROR_FILE_NOT_FOUND)return;
  check(status,"Cannot open Windows startup entries");
 }else{
  check(RegCreateKeyExW(HKEY_CURRENT_USER,registry_path.c_str(),0,nullptr,0,KEY_READ|KEY_WRITE,
                        nullptr,&run.handle,nullptr),"Cannot open Windows startup entries");
 }
 std::map<std::wstring,std::wstring> planned;
 const auto absolute=std::filesystem::absolute(executable).lexically_normal().wstring();
 if(entries.size()&&(!std::filesystem::is_regular_file(executable)))
  throw std::runtime_error("Executable is not available for startup registration");
 for(const auto& entry:entries){
  const auto name=wide(entry.value_name);
  const auto command=quote_windows_argument(absolute)+L" --auto-mirror "+quote_windows_argument(wide(entry.profile_name));
  planned.emplace(name,command);
 }
 // Enumerate only our names. Never touch other applications' startup values.
 std::vector<std::wstring> existing;
 for(DWORD index=0;;++index){
  wchar_t name[16384]{};
  DWORD length=16383;
  const auto status=RegEnumValueW(run.handle,index,name,&length,nullptr,nullptr,nullptr,nullptr);
  if(status==ERROR_NO_MORE_ITEMS)break;
  check(status,"Cannot enumerate Windows startup entries");
  std::wstring value(name,length);
  if(value.rfind(kPrefix,0)==0)existing.push_back(std::move(value));
 }
 // Write new commands before removing obsolete ones.
 for(const auto& [name,command]:planned){
  check(RegSetValueExW(run.handle,name.c_str(),0,REG_SZ,
       reinterpret_cast<const BYTE*>(command.c_str()),
       static_cast<DWORD>((command.size()+1)*sizeof(wchar_t))),
       "Cannot register Windows login startup");
 }
 for(const auto& value:existing)
  if(!planned.count(value))
   check(RegDeleteValueW(run.handle,value.c_str()),"Cannot remove obsolete Windows startup entry");
}
}
