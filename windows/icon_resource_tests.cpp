#include <windows.h>
#include <shellapi.h>
#include <cassert>
#include <filesystem>
#include <iostream>
int main(){
 wchar_t executable[32768]{};
 const auto length=GetModuleFileNameW(nullptr,executable,32768);
 assert(length>0&&length<32768);
 const auto target=std::filesystem::path(executable).parent_path()/L"synchrorep.exe";
 HMODULE resource=LoadLibraryExW(target.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE);
 assert(resource);
 HRSRC icon=FindResourceW(resource,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(14));
 assert(icon);
 assert(SizeofResource(resource,icon)>0);
 FreeLibrary(resource);
 HICON large=nullptr;
 const auto found=ExtractIconExW(target.c_str(),0,&large,nullptr,1);
 assert(found>0&&large);
 DestroyIcon(large);
 std::cout<<"Windows executable icon resource passed\n";
}
