#include "synchrorep/recycle_bin.hpp"
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <memory>

namespace synchrorep {
namespace {
struct Apartment {
 bool initialized=false;
 Apartment(){
  const HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if(FAILED(result))throw std::runtime_error("Windows Shell requires STA COM initialization");
  initialized=true;
 }
 ~Apartment(){if(initialized)CoUninitialize();}
};
template<class T> struct ComRelease {
 void operator()(T* value)const noexcept{if(value)value->Release();}
};
void require(HRESULT result,const char* action){
 if(FAILED(result))
  throw std::runtime_error(std::string(action)+" (HRESULT "+std::to_string(static_cast<unsigned long>(result))+")");
}
}
bool supports_native_recycle(const std::filesystem::path& folder) noexcept {
 try {
  // Some MinGW STL builds turn a UNC path into a drive-qualified path when
  // computing absolute(). Reject UNC/WSL paths BEFORE that conversion.
  const auto original=folder.native();
  if(original.size()>=2 && (original[0]==L'\\'||original[0]==L'/') &&
     (original[1]==L'\\'||original[1]==L'/'))return false;
  auto path=std::filesystem::absolute(folder).lexically_normal();
  const auto native=path.native();
  if(native.size()<3||native[1]!=L':'||
     (native[2]!=L'\\'&&native[2]!=L'/'))return false;
  const std::wstring volume{native[0],L':',L'\\'};
  return GetDriveTypeW(volume.c_str())==DRIVE_FIXED;
 }catch(...){return false;}
}
void move_to_native_recycle(const std::filesystem::path& absolute_path){
 if(!supports_native_recycle(absolute_path))
  throw std::runtime_error("Windows Recycle Bin is unsupported for this location");
 if(!std::filesystem::exists(absolute_path))
  throw std::runtime_error("Cannot recycle a missing file");
 Apartment com;
 IFileOperation* raw=nullptr;
 require(CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,
                         IID_IFileOperation,reinterpret_cast<void**>(&raw)),
         "Cannot open Windows Recycle Bin file operation");
 std::unique_ptr<IFileOperation,ComRelease<IFileOperation>> operation(raw);
 // FOFX_RECYCLEONDELETE (Windows 8+) explicitly requests recycling.
 // FOF_WANTNUKEWARNING overrides the silent flag if recycling is unavailable.
 require(operation->SetOperationFlags(FOFX_RECYCLEONDELETE|FOF_ALLOWUNDO|
         FOF_SILENT|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_WANTNUKEWARNING),
         "Cannot configure recycle operation");
 IShellItem* raw_item=nullptr;
 require(SHCreateItemFromParsingName(absolute_path.c_str(),nullptr,
                                    IID_IShellItem,reinterpret_cast<void**>(&raw_item)),
         "Cannot locate Windows Shell item");
 std::unique_ptr<IShellItem,ComRelease<IShellItem>> item(raw_item);
 require(operation->DeleteItem(item.get(),nullptr),"Cannot queue recycle operation");
 require(operation->PerformOperations(),"Failed moving file to Recycle Bin");
 BOOL aborted=FALSE;
 require(operation->GetAnyOperationsAborted(&aborted),"Cannot check recycle operation");
 if(aborted||std::filesystem::exists(absolute_path))
  throw std::runtime_error("Windows Recycle Bin operation incomplete; no permanent fallback");
}
}
#else
namespace synchrorep {
bool supports_native_recycle(const std::filesystem::path&) noexcept {return false;}
void move_to_native_recycle(const std::filesystem::path&){
 throw std::runtime_error("Native recycle bin is available only on Windows local drives");
}
}
#endif
