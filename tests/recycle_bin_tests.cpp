#include "synchrorep/recycle_bin.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
using namespace synchrorep;
namespace fs=std::filesystem;
int main(){
#ifdef _WIN32
 assert(!supports_native_recycle(fs::path(L"\\\\wsl.localhost\\Ubuntu\\home")));
 assert(!supports_native_recycle(fs::path(L"\\\\fileserver\\share\\doc")));
 // Explicit opt-in only: requires an actual Windows interactive session.
 // Keeps CI service runners from being asked to access an unavailable Shell.
 if(const char* enabled=std::getenv("SYNCHROREP_TEST_WINDOWS_RECYCLE");
    enabled && std::string(enabled)=="1"){
  auto root=fs::temp_directory_path()/("synchrorep-recycle-smoke-"+
   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  if(!supports_native_recycle(root))
   throw std::runtime_error("Windows smoke test requested on non-recyclable volume");
  fs::create_directories(root);
  auto file=root/"synchrorep-test.txt";
  {std::ofstream out(file);out<<"Synchrorep integration test"; }
  move_to_native_recycle(file);
  assert(!fs::exists(file));
  fs::remove(root);
  std::cout<<"Native Windows recycle smoke test passed\n";
 }
#else
 assert(!supports_native_recycle(fs::temp_directory_path()));
 bool refused=false;
 try{move_to_native_recycle(fs::temp_directory_path()/"not-a-file");}
 catch(const std::exception&){refused=true;}
 assert(refused);
#endif
 std::cout<<"Recycle Bin portability checks passed\n";
}
