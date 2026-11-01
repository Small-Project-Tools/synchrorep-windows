#include "synchrorep/endpoint.hpp"
#include "synchrorep/remote_endpoint.hpp"
#include "synchrorep/recycle_bin.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cstdint>
#include <limits>
#include <regex>
#include <stdexcept>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace synchrorep {
namespace {
bool starts(const std::string& value, std::string_view prefix) {
 return value.compare(0,prefix.size(),prefix)==0;
}
bool valid_relative(const std::string& relative) {
 if(relative.empty() || relative.front()=='/' || relative.front()=='\\')return false;
 fs::path p=fs::path(std::u8string(reinterpret_cast<const char8_t*>(relative.data()),relative.size()));
 if(p.is_absolute() || p.has_root_name())return false;
 for(const auto& part:p)if(part==".." || part==".")return false;
 if(relative.find('\\')!=std::string::npos)return false;
 return true;
}
#ifdef _WIN32
// FILETIME stores 100 ns ticks since 1601-01-01 UTC. Windows' filesystem
// clock does not expose C++20 to_sys/from_sys in every MSVC version.
constexpr std::uint64_t windows_ticks_per_second = 10000000ULL;
constexpr std::int64_t windows_to_unix_seconds = 11644473600LL;
std::int64_t timestamp(const fs::path& p) {
 WIN32_FILE_ATTRIBUTE_DATA data{};
 if(!GetFileAttributesExW(p.c_str(),GetFileExInfoStandard,&data))
  throw fs::filesystem_error("Cannot read file timestamp",p,
   std::error_code(GetLastError(),std::system_category()));
 ULARGE_INTEGER ticks{};
 ticks.LowPart=data.ftLastWriteTime.dwLowDateTime;
 ticks.HighPart=data.ftLastWriteTime.dwHighDateTime;
 return static_cast<std::int64_t>(ticks.QuadPart/windows_ticks_per_second)
  -windows_to_unix_seconds;
}
void restore_timestamp(const fs::path& p,std::int64_t modified) {
 constexpr auto max_seconds=static_cast<std::int64_t>(
  std::numeric_limits<std::uint64_t>::max()/windows_ticks_per_second);
 if(modified < -windows_to_unix_seconds ||
    modified > max_seconds-windows_to_unix_seconds)
  throw std::out_of_range("File timestamp is outside the Windows FILETIME range");
 ULARGE_INTEGER ticks{};
 ticks.QuadPart=static_cast<std::uint64_t>(modified+windows_to_unix_seconds)
  *windows_ticks_per_second;
 FILETIME value{ticks.LowPart,ticks.HighPart};
 const HANDLE file=CreateFileW(p.c_str(),FILE_WRITE_ATTRIBUTES,
  FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
  FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)
  throw fs::filesystem_error("Cannot open file to restore timestamp",p,
   std::error_code(GetLastError(),std::system_category()));
 const BOOL success=SetFileTime(file,nullptr,nullptr,&value);
 const DWORD error=success?ERROR_SUCCESS:GetLastError();
 CloseHandle(file);
 if(!success)
  throw fs::filesystem_error("Cannot restore file timestamp",p,
   std::error_code(error,std::system_category()));
}
#else
std::int64_t timestamp(const fs::path& p) {
 return std::chrono::duration_cast<std::chrono::seconds>(
  fs::file_time_type::clock::to_sys(fs::last_write_time(p)).time_since_epoch()).count();
}
void restore_timestamp(const fs::path& p,std::int64_t modified) {
 fs::last_write_time(p,fs::file_time_type::clock::from_sys(
  std::chrono::system_clock::time_point{std::chrono::seconds(modified)}));
}
#endif
Entry stat(const fs::path& path) {
 std::error_code ec;
 auto s=fs::symlink_status(path,ec);
 if(ec || s.type()==fs::file_type::not_found)throw std::runtime_error("Filesystem entry disappeared: "+path.string());
 if(fs::is_symlink(s))throw std::runtime_error("Symbolic links are not supported: "+path.string());
 if(!fs::is_directory(s) && !fs::is_regular_file(s))throw std::runtime_error("Unsupported filesystem object: "+path.string());
 Entry e; e.exists=true;e.directory=fs::is_directory(s);
 if(!e.directory){e.size=fs::file_size(path);e.modified=timestamp(path);}
 return e;
}
void atomic_replace(const fs::path& tmp,const fs::path& dst) {
 std::error_code ec;
 fs::rename(tmp,dst,ec);
 if(!ec)return;
#ifdef _WIN32
 // Windows rename() does not replace an existing destination.
 if(fs::exists(dst) && ReplaceFileW(dst.c_str(),tmp.c_str(),nullptr,0,nullptr,nullptr))return;
#endif
 throw fs::filesystem_error("Unable to atomically replace destination",tmp,dst,ec);
}
std::string native_identity(const fs::path& root) {
 auto u8=root.generic_u8string();
 return "file:"+std::string(u8.begin(),u8.end());
}
}
// MinGW's std::filesystem::absolute() can treat a valid UNC path as relative
// and prefix the current drive, e.g. \\wsl.localhost -> C:\wsl.localhost.
// Resolve Windows UNC roots with the OS API before accessing the filesystem.
fs::path normalize_endpoint_root(fs::path root) {
#ifdef _WIN32
 const auto native=root.native();
 if(native.size()>=2 &&
    (native[0]==L'\\' || native[0]==L'/') &&
    (native[1]==L'\\' || native[1]==L'/')) {
  const DWORD capacity=GetFullPathNameW(native.c_str(),0,nullptr,nullptr);
  if(capacity==0 || capacity>32767)
   throw std::runtime_error("Cannot resolve UNC endpoint directory");
  std::wstring resolved(capacity,L'\0');
  const DWORD written=GetFullPathNameW(native.c_str(),capacity,resolved.data(),nullptr);
  if(written==0 || written>=capacity)
   throw std::runtime_error("Cannot normalize UNC endpoint directory");
  resolved.resize(written);
  if(resolved.size()<2 || resolved[0]!=L'\\' || resolved[1]!=L'\\')
   throw std::runtime_error("UNC endpoint lost its network root");
  return fs::path(std::move(resolved));
 }
#endif
 return fs::absolute(std::move(root)).lexically_normal();
}
LocalEndpoint::LocalEndpoint(fs::path root) : root_(normalize_endpoint_root(std::move(root))) {
 if(fs::is_symlink(fs::symlink_status(root_)))throw std::runtime_error("Directory root must not be a symbolic link");
 if(!fs::is_directory(root_))throw std::runtime_error("Directory does not exist: "+root_.string());
}
std::string LocalEndpoint::identity()const {return native_identity(root_);}
fs::path LocalEndpoint::within(const std::string& relative) const {
 if(!valid_relative(relative))throw std::invalid_argument("Invalid relative path: "+relative);
 return root_/fs::path(std::u8string(reinterpret_cast<const char8_t*>(relative.data()),relative.size()));
}
Snapshot LocalEndpoint::scan()const{
 Snapshot found;
 static const std::regex unfinished(R"(\.synchrorep-(?:write-[0-9]+\.tmp|upload-[0-9]+)$)");
 for(auto i=fs::recursive_directory_iterator(root_);i!=fs::recursive_directory_iterator();++i) {
  // An unclean shutdown can leave a temporary file. It must never be
  // propagated to the opposite endpoint as user data.
  const auto name=i->path().filename().string();
  if(std::regex_search(name,unfinished))
   throw std::runtime_error("Interrupted transfer artifact detected; inspect and remove manually: "+i->path().string());
  auto key=i->path().lexically_relative(root_).generic_u8string();
  found.emplace(std::string(key.begin(),key.end()),stat(i->path()));
 }
 return found;
}
std::unique_ptr<std::istream> LocalEndpoint::read(const std::string& relative)const {
 auto file=within(relative);
 auto stream=std::make_unique<std::ifstream>(file,std::ios::binary);
 if(!*stream)throw std::runtime_error("Cannot read file: "+file.string());
 return stream;
}
void LocalEndpoint::write(const std::string& relative,std::istream& input,const Entry& source) {
 if(source.directory)throw std::invalid_argument("Cannot write directory as a file");
 auto target=within(relative);
 fs::create_directories(target.parent_path());
 auto tmp=target;
 tmp+=".synchrorep-write-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".tmp";
 if(fs::exists(tmp))throw std::runtime_error("Temporary target already exists");
 try {
  {
   std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
   if(!out)throw std::runtime_error("Cannot create temporary file: "+tmp.string());
   out<<input.rdbuf();out.flush();
   if(!out || input.bad())throw std::runtime_error("Incomplete transfer: "+relative);
  }
  // Do not promote a short transfer. The endpoint must still have the
  // original file intact if a remote or local input fails.
  if(fs::file_size(tmp)!=source.size)throw std::runtime_error("Transfer size mismatch: "+relative);
  atomic_replace(tmp,target);
  // Preserve native mtime for local-local and WSL UNC transfers.
  restore_timestamp(target,source.modified);
 } catch(...) {std::error_code ignored;fs::remove(tmp,ignored);throw;}
}
void LocalEndpoint::make_directory(const std::string& relative){fs::create_directories(within(relative));}
void LocalEndpoint::remove(const std::string& relative){
 const auto p=within(relative);auto s=stat(p);
 if(!fs::remove(p))throw std::runtime_error("Could not delete "+p.string());
 (void)s;
}
bool LocalEndpoint::supports_recycle_bin()const {
 return supports_native_recycle(root_);
}
void LocalEndpoint::recycle(const std::string& relative){
 if(!supports_recycle_bin())
  throw std::runtime_error("Native Recycle Bin unsupported for this endpoint");
 const auto target=within(relative);
 // Reject a symlink or junction before delegating to the Shell API.
 const auto status=fs::symlink_status(target);
 if(fs::is_symlink(status))throw std::runtime_error("Cannot recycle symbolic link");
 move_to_native_recycle(target);
}
EndpointKind endpoint_kind(const std::string& location) {
 if(starts(location,"wsl://"))return EndpointKind::Wsl;
 if(starts(location,"ftp://"))return EndpointKind::Ftp;
 if(starts(location,"sftp://"))return EndpointKind::Sftp;
 if(location.find("://")!=std::string::npos)throw std::invalid_argument("Unsupported URI scheme");
 return EndpointKind::Local;
}
fs::path wsl_unc_path(const std::string& uri) {
 if(endpoint_kind(uri)!=EndpointKind::Wsl)throw std::invalid_argument("Expected wsl:// URI");
 auto rest=uri.substr(6);
 const auto slash=rest.find('/');
 const auto distro=rest.substr(0,slash);
 if(distro.empty() || distro=="." || distro==".." || distro.find_first_of("\\:@%?#")!=std::string::npos)
  throw std::invalid_argument("Invalid WSL distribution name");
 auto sub=slash==std::string::npos?std::string{}:rest.substr(slash+1);
 // Reject both path traversal and URL-encoded paths; no implicit decoding.
 if(sub.find('%')!=std::string::npos)throw std::invalid_argument("Encoded WSL URI unsupported");
 if(!sub.empty()&&!valid_relative(sub))throw std::invalid_argument("Invalid WSL path");
#ifdef _WIN32
 std::string p="\\\\wsl.localhost\\"+distro;
 if(!sub.empty())p+='\\';
 for(char c:sub)p+=c=='/'?'\\':c;
 return fs::path(std::u8string(reinterpret_cast<const char8_t*>(p.data()),p.size()));
#else
 throw std::runtime_error("wsl:// requires Windows and WSL");
#endif
}
std::unique_ptr<Endpoint> open_endpoint(const std::string& location) {
 switch(endpoint_kind(location)) {
 case EndpointKind::Local: return std::make_unique<LocalEndpoint>(fs::path(std::u8string(reinterpret_cast<const char8_t*>(location.data()),location.size())));
 case EndpointKind::Wsl: return std::make_unique<LocalEndpoint>(wsl_unc_path(location));
 case EndpointKind::Ftp: case EndpointKind::Sftp:{
  auto env=[](const char* key){const char* v=std::getenv(key);return v?std::string(v):std::string{};};
  const char* prefix=endpoint_kind(location)==EndpointKind::Ftp?"SYNCHROREP_FTP_":"SYNCHROREP_SFTP_";
  RemoteCredentials credentials;
  credentials.username=env((std::string(prefix)+"USER").c_str());
  credentials.password=env((std::string(prefix)+"PASSWORD").c_str());
  credentials.known_hosts=env("SYNCHROREP_SFTP_KNOWN_HOSTS");
  credentials.private_key=env("SYNCHROREP_SFTP_PRIVATE_KEY");
  return std::make_unique<RemoteEndpoint>(location,std::move(credentials));
 }
 }
 throw std::runtime_error("Unknown endpoint");
}
}
