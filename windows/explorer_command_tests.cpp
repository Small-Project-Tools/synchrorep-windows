#include "explorer_clsid.hpp"
#include <windows.h>
#include <shobjidl.h>
#include <cassert>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iostream>
using Factory = HRESULT (WINAPI*)(REFCLSID,REFIID,void**);
using CanUnload = HRESULT (WINAPI*)();
int main(){
 wchar_t executable[32768]{};
 auto length=GetModuleFileNameW(nullptr,executable,32768);
 assert(length>0&&length<32768);
 auto dll=std::filesystem::path(executable).parent_path()/L"SynchrorepExplorer.dll";
 HMODULE module=LoadLibraryW(dll.c_str());
 if(!module)return 1;
 auto get=reinterpret_cast<Factory>(GetProcAddress(module,"DllGetClassObject"));
 auto unload=reinterpret_cast<CanUnload>(GetProcAddress(module,"DllCanUnloadNow"));
 assert(get&&unload);
 assert(unload()==S_OK);
 IClassFactory* factory=nullptr;
 assert(get(synchrorep::winui::ExplorerCommandClsid,IID_IClassFactory,
            reinterpret_cast<void**>(&factory))==S_OK);
 assert(factory&&unload()==S_FALSE);
 IExplorerCommand* command=nullptr;
 assert(factory->CreateInstance(nullptr,IID_IExplorerCommand,
            reinterpret_cast<void**>(&command))==S_OK);
 LPWSTR title=nullptr;
 assert(command->GetTitle(nullptr,&title)==S_OK);
 assert(title&&std::wcscmp(title,L"Synchronize with Synchrorep")==0);
 CoTaskMemFree(title);
 LPWSTR icon=nullptr;
 assert(command->GetIcon(nullptr,nullptr)==E_POINTER);
 assert(command->GetIcon(nullptr,&icon)==S_OK);
 assert(icon);
 const std::wstring iconLocation(icon);
 CoTaskMemFree(icon);
 assert(iconLocation.size()>2&&iconLocation.substr(iconLocation.size()-2)==L",0");
 assert(iconLocation.find(L"synchrorep.exe")!=std::wstring::npos);
 EXPCMDSTATE state=ECS_ENABLED;
 assert(command->GetState(nullptr,FALSE,&state)==S_OK && state==ECS_HIDDEN);
 GUID canonical{};
 assert(command->GetCanonicalName(&canonical)==S_OK);
 assert(IsEqualGUID(canonical,synchrorep::winui::ExplorerCommandClsid));
 command->Release();
 factory->Release();
 assert(unload()==S_OK);
 FreeLibrary(module);
 std::cout<<"Explorer COM command contract passed\n";
 return 0;
}
