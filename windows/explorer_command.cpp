#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include "explorer_clsid.hpp"
#include "windows_args.hpp"
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <atomic>
#include <new>
#include <cwchar>
#include <cstring>
#include <filesystem>
#include <string>

namespace {
constexpr CLSID kSynchrorepCommand=synchrorep::winui::ExplorerCommandClsid;
HINSTANCE g_module=nullptr;
std::atomic<long> g_objects{0};
std::atomic<long> g_locks{0};

HRESULT duplicate_string(const wchar_t* source,PWSTR* output){
 if(!output)return E_POINTER;
 *output=nullptr;
 const auto characters=std::wcslen(source)+1;
 auto* memory=static_cast<wchar_t*>(CoTaskMemAlloc(characters*sizeof(wchar_t)));
 if(!memory)return E_OUTOFMEMORY;
 std::memcpy(memory,source,characters*sizeof(wchar_t));
 *output=memory;
 return S_OK;
}
HRESULT selected_folder(IShellItemArray* selection,std::wstring& directory){
 if(!selection)return E_INVALIDARG;
 DWORD count=0;
 HRESULT status=selection->GetCount(&count);
 if(FAILED(status))return status;
 if(count!=1)return E_INVALIDARG;
 IShellItem* item=nullptr;
 status=selection->GetItemAt(0,&item);
 if(FAILED(status))return status;
 SFGAOF attributes=SFGAO_FOLDER;
 status=item->GetAttributes(SFGAO_FOLDER,&attributes);
 if(SUCCEEDED(status)&&!(attributes&SFGAO_FOLDER))status=E_INVALIDARG;
 if(SUCCEEDED(status)){
  PWSTR path=nullptr;
  status=item->GetDisplayName(SIGDN_FILESYSPATH,&path);
  if(SUCCEEDED(status)){
   directory=path;
   CoTaskMemFree(path);
  }
 }
 item->Release();
 return status;
}
class Command final : public IExplorerCommand{
 std::atomic<ULONG> refs_{1};
public:
 Command(){++g_objects;}
 ~Command(){--g_objects;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output)override{
  if(!output)return E_POINTER;
  *output=nullptr;
  if(IsEqualIID(iid,IID_IUnknown)||IsEqualIID(iid,IID_IExplorerCommand)){
   *output=static_cast<IExplorerCommand*>(this);
   AddRef();return S_OK;
  }
  return E_NOINTERFACE;
 }
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
 ULONG STDMETHODCALLTYPE Release()override{
  const auto refs=--refs_;
  if(!refs)delete this;
  return refs;
 }
 HRESULT STDMETHODCALLTYPE GetTitle(IShellItemArray*,LPWSTR* title)override{
  return duplicate_string(L"Synchronize with Synchrorep",title);
 }
 HRESULT STDMETHODCALLTYPE GetIcon(IShellItemArray*,LPWSTR* icon)override{
  if(!icon)return E_POINTER;
  *icon=nullptr;
  wchar_t module[MAX_PATH]{};
  const DWORD count=GetModuleFileNameW(g_module,module,MAX_PATH);
  if(count==0||count>=MAX_PATH)
   return HRESULT_FROM_WIN32(GetLastError()==ERROR_SUCCESS?ERROR_INSUFFICIENT_BUFFER:GetLastError());
  const auto executable=std::filesystem::path(module).parent_path()/L"synchrorep.exe";
  std::error_code ec;
  if(!std::filesystem::is_regular_file(executable,ec))
   return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  return duplicate_string((executable.wstring()+L",0").c_str(),icon);
 }
 HRESULT STDMETHODCALLTYPE GetToolTip(IShellItemArray*,LPWSTR* tip)override{
  return duplicate_string(L"Synchronize this folder; choose a destination if not configured",tip);
 }
 HRESULT STDMETHODCALLTYPE GetCanonicalName(GUID* name)override{
  if(!name)return E_POINTER;
  *name=kSynchrorepCommand;return S_OK;
 }
 HRESULT STDMETHODCALLTYPE GetState(IShellItemArray* selection,BOOL,EXPCMDSTATE* state)override{
  if(!state)return E_POINTER;
  *state=ECS_HIDDEN;
  std::wstring path;
  if(SUCCEEDED(selected_folder(selection,path)))*state=ECS_ENABLED;
  return S_OK;
 }
 HRESULT STDMETHODCALLTYPE Invoke(IShellItemArray* selection,IBindCtx*)override{
  std::wstring directory;
  HRESULT result=selected_folder(selection,directory);
  if(FAILED(result))return result;
  wchar_t dll[MAX_PATH]{};
  DWORD length=GetModuleFileNameW(g_module,dll,MAX_PATH);
  if(!length||length>=MAX_PATH)return HRESULT_FROM_WIN32(GetLastError()?GetLastError():ERROR_BUFFER_OVERFLOW);
  std::wstring binary=dll;
  const auto separator=binary.find_last_of(L"\\/");
  if(separator==std::wstring::npos)return E_FAIL;
  binary.resize(separator+1);
  binary+=L"synchrorep.exe";
  const auto arguments=synchrorep::winui::quote_windows_argument(directory);
  SHELLEXECUTEINFOW request{};
  request.cbSize=sizeof(request);
  request.fMask=SEE_MASK_NOASYNC;
  request.lpVerb=L"open";
  request.lpFile=binary.c_str();
  request.lpParameters=arguments.c_str();
  request.nShow=SW_SHOWNORMAL;
  return ShellExecuteExW(&request)?S_OK:HRESULT_FROM_WIN32(GetLastError());
 }
 HRESULT STDMETHODCALLTYPE GetFlags(EXPCMDFLAGS* flags)override{
  if(!flags)return E_POINTER;
  *flags=ECF_DEFAULT;return S_OK;
 }
 HRESULT STDMETHODCALLTYPE EnumSubCommands(IEnumExplorerCommand** commands)override{
  if(!commands)return E_POINTER;
  *commands=nullptr;return E_NOTIMPL;
 }
};
class Factory final:public IClassFactory {
 std::atomic<ULONG> refs_{1};
public:
 Factory(){++g_objects;}
 ~Factory(){--g_objects;}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** value)override{
  if(!value)return E_POINTER;
  *value=nullptr;
  if(IsEqualIID(iid,IID_IUnknown)||IsEqualIID(iid,IID_IClassFactory)){
   *value=static_cast<IClassFactory*>(this);AddRef();return S_OK;
  }
  return E_NOINTERFACE;
 }
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
 ULONG STDMETHODCALLTYPE Release()override{
  auto refs=--refs_;if(!refs)delete this;return refs;
 }
 HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* parent,REFIID iid,void** out)override{
  if(!out)return E_POINTER;
  *out=nullptr;
  if(parent)return CLASS_E_NOAGGREGATION;
  auto* command=new(std::nothrow) Command;
  if(!command)return E_OUTOFMEMORY;
  auto result=command->QueryInterface(iid,out);
  command->Release();
  return result;
 }
 HRESULT STDMETHODCALLTYPE LockServer(BOOL lock)override{
  if(lock)++g_locks;else --g_locks;
  return S_OK;
 }
};
}
extern "C" BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){
 if(reason==DLL_PROCESS_ATTACH){
  g_module=module;
  DisableThreadLibraryCalls(module);
 }
 return TRUE;
}
extern "C" __declspec(dllexport) HRESULT WINAPI DllGetClassObject(
 REFCLSID clsid,REFIID iid,void** result){
 if(!result)return E_POINTER;
 *result=nullptr;
 if(!IsEqualCLSID(clsid,kSynchrorepCommand))return CLASS_E_CLASSNOTAVAILABLE;
 auto* factory=new(std::nothrow) Factory;
 if(!factory)return E_OUTOFMEMORY;
 auto status=factory->QueryInterface(iid,result);
 factory->Release();
 return status;
}
extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(){
 return g_objects.load()==0&&g_locks.load()==0?S_OK:S_FALSE;
}
