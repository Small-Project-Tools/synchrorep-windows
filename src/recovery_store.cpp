// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/recovery_store.hpp"
#include "synchrorep/sha256.hpp"
#include <array>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace synchrorep {
namespace {
void durable(const fs::path& file) {
#ifdef _WIN32
 HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,
   FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
   FILE_ATTRIBUTE_NORMAL,nullptr);
 if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open recovery file for flush");
 auto ok=FlushFileBuffers(handle);
 CloseHandle(handle);
 if(!ok)throw std::runtime_error("Cannot flush recovery file");
#else
 int fd=open(file.c_str(),O_RDONLY);
 if(fd<0)throw std::runtime_error("Cannot open recovery file for fsync");
 auto result=fsync(fd);
 close(fd);
 if(result!=0)throw std::runtime_error("Cannot fsync recovery file");
#endif
}
fs::path relative_path(const std::string& input){
 if(input.empty()||input.front()=='/'||input.find('\\')!=std::string::npos ||
    input.find_first_of("\r\n:*?\"<>|")!=std::string::npos ||
    std::find(input.begin(),input.end(),'\0')!=input.end())
  throw std::invalid_argument("Unsafe archive path");
 fs::path path=fs::path(std::u8string(reinterpret_cast<const char8_t*>(input.data()),input.size()));
 if(path.is_absolute()||path.has_root_name())throw std::invalid_argument("Absolute archive path");
 for(auto part:path)if(part==".."||part=="."||part.empty())
  throw std::invalid_argument("Traversal in archive path");
 return path;
}
std::string utf8(const fs::path& path) {
 auto native=path.generic_u8string();
 return {native.begin(),native.end()};
}
}
RecoveryStore::RecoveryStore(fs::path directory)
 : directory_(fs::absolute(std::move(directory)).lexically_normal()),
   content_root_(directory_/"data"),manifest_(directory_/"manifest.tsv") {
 if(fs::exists(directory_))throw std::runtime_error("Recovery run already exists");
 fs::create_directories(content_root_);
 {
  std::ofstream manifest(manifest_,std::ios::binary|std::ios::trunc);
  if(!manifest)throw std::runtime_error("Cannot create recovery manifest");
  manifest<<"SYNCHROREP-RECOVERY1\n";
  manifest.flush();
  if(!manifest)throw std::runtime_error("Cannot write recovery manifest");
 }
 durable(manifest_);
}
void RecoveryStore::preserve(const Endpoint& endpoint,const std::string& relative,
                             const Entry& expected,const std::string& reason){
 if(!expected.exists)throw std::invalid_argument("Cannot archive missing entry");
 auto relative_file=relative_path(relative);
 auto target=content_root_/relative_file;
 // The endpoint is a folder-specific archive store: its directory must
 // already encode the destination side and run ID, not be shared across sides.
 if(fs::exists(target) && (!expected.directory||!fs::is_directory(target)))
  throw std::runtime_error("Duplicate recovery archive path");
 fs::create_directories(target.parent_path());
 if(expected.directory){
  // Previous file backups may already have created a parent directory.
  if(!fs::exists(target))fs::create_directory(target);
 }else{
  if(expected.hash.size()!=64)throw std::runtime_error("Missing expected SHA-256 before destructive operation");
  auto stream=endpoint.read(relative);
  auto tmp=target;tmp+=".partial";
  {
   std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
   if(!out)throw std::runtime_error("Cannot stage recovery backup");
   std::array<char,65536> buffer{};
   std::uintmax_t count=0;
   for(;;){
    stream->read(buffer.data(),buffer.size());
    auto n=stream->gcount();
    if(stream->bad())throw std::runtime_error("Cannot read recovery source");
    if(n>0){
     count+=static_cast<std::uintmax_t>(n);
     if(count>expected.size)throw std::runtime_error("Recovery source size changed");
     out.write(buffer.data(),n);
     if(!out)throw std::runtime_error("Cannot write recovery backup");
    }
    if(stream->eof())break;
    if(!*stream)throw std::runtime_error("Recovery source truncated");
   }
   out.flush();
   if(!out||count!=expected.size)throw std::runtime_error("Incomplete recovery backup");
  }
  durable(tmp);
  {
   std::ifstream input(tmp,std::ios::binary);
   if(!input||sha256(input,expected.size)!=expected.hash)
    throw std::runtime_error("Recovery backup SHA-256 mismatch; destination untouched");
  } // Windows cannot rename a file while it is open for reading.
  fs::rename(tmp,target);
 }
 {
  std::ofstream out(manifest_,std::ios::binary|std::ios::app);
  if(!out)throw std::runtime_error("Cannot append recovery manifest");
  // Use quoted values to safely round-trip filenames containing whitespace.
  out<<std::quoted(reason)<<' '<<std::quoted(endpoint.identity())<<' '
     <<std::quoted(relative)<<' '<<expected.directory<<' '
     <<expected.size<<' '<<std::quoted(expected.hash)<<' '
     <<std::quoted(utf8(target.lexically_relative(directory_)))<<'\n';
  out.flush();
  if(!out)throw std::runtime_error("Cannot flush recovery manifest");
 }
 durable(manifest_);
}
}
