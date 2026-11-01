#include "synchrorep/recovery_catalog.hpp"
#include "synchrorep/sha256.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <set>
#include <stdexcept>
#include <system_error>
namespace synchrorep {
namespace {
fs::path from_utf8(const std::string& input){
 return fs::path(std::u8string(reinterpret_cast<const char8_t*>(input.data()),input.size()));
}
void validate_name(const std::string& value){
 if(value.empty()||value.front()=='/'||value.find('\\')!=std::string::npos||
    value.find_first_of("\r\n:*?\"<>|")!=std::string::npos||
    value.find('\0')!=std::string::npos)throw std::runtime_error("Invalid path in recovery manifest");
 fs::path p=from_utf8(value);
 if(p.is_absolute()||p.has_root_name())throw std::runtime_error("Absolute recovery manifest path");
 for(const auto& part:p)if(part.empty()||part=="."||part=="..")
  throw std::runtime_error("Unsafe recovery manifest component");
}
void no_symlinks(const fs::path& target){
 // Walk path components including the root and reject links and junctions.
 fs::path current;
 for(const auto& part:fs::absolute(target).lexically_normal()){
  current/=part;
  std::error_code ec;
  auto state=fs::symlink_status(current,ec);
  if(!ec && fs::is_symlink(state))
   throw std::runtime_error("Recovery path contains a symbolic link");
  if(ec && ec!=std::errc::no_such_file_or_directory)
   throw fs::filesystem_error("Recovery path inaccessible",current,ec);
 }
}
bool valid_sha(const std::string& hash){
 return hash.size()==64&&std::all_of(hash.begin(),hash.end(),[](char c){
  return (c>='0'&&c<='9')||(c>='a'&&c<='f');
 });
}
std::vector<RecoveryItem> one_side(const fs::path& side){
 no_symlinks(side);
 const auto manifest=side/"manifest.tsv", data=side/"data";
 no_symlinks(manifest);
 no_symlinks(data);
 if(!fs::is_regular_file(manifest)||!fs::is_directory(data))
  throw std::runtime_error("Invalid recovery archive layout");
 std::ifstream in(manifest,std::ios::binary);
 if(!in)throw std::runtime_error("Cannot read recovery manifest");
 std::string header;std::getline(in,header);
 if(header!="SYNCHROREP-RECOVERY1")throw std::runtime_error("Invalid recovery manifest header");
 std::vector<RecoveryItem> items;std::set<std::string> paths;
 std::string line;
 while(std::getline(in,line)){
  if(line.empty())throw std::runtime_error("Invalid empty recovery record");
  std::istringstream row(line);
  RecoveryItem item;
  int directory=0;std::string stored;
  if(!(row>>std::quoted(item.reason)>>std::quoted(item.endpoint)>>
      std::quoted(item.relative)>>directory>>item.size>>std::quoted(item.hash)>>
      std::quoted(stored)) || !(row>>std::ws).eof())
   throw std::runtime_error("Invalid recovery record");
  if(directory!=0&&directory!=1)throw std::runtime_error("Invalid recovery type");
  validate_name(item.relative);validate_name(stored);
  if(stored!="data/"+item.relative)
   throw std::runtime_error("Recovery path does not correspond to manifest entry");
  if((directory==0&&!valid_sha(item.hash))||(directory==1&&!item.hash.empty()))
   throw std::runtime_error("Invalid archive fingerprint");
  if(!paths.insert(item.relative).second)
   throw std::runtime_error("Duplicate recovery entry");
  item.directory=directory!=0;
  item.archive_side=side;
  item.side=side.filename().string();
  items.push_back(std::move(item));
  if(items.size()>100000)throw std::runtime_error("Too many recovery records");
 }
 if(in.bad())throw std::runtime_error("Recovery manifest read failed");
 return items;
}
}
std::vector<RecoveryItem> RecoveryCatalog::list(const fs::path& recovery_root){
 if(!fs::exists(recovery_root))return {};
 no_symlinks(recovery_root);
 if(!fs::is_directory(recovery_root))throw std::runtime_error("Recovery root is not a directory");
 std::vector<RecoveryItem> found;
 for(const auto& state:fs::directory_iterator(recovery_root)){
  if(!state.is_directory()||state.is_symlink())
   throw std::runtime_error("Unexpected recovery state entry");
  for(const auto& run:fs::directory_iterator(state.path())){
   if(!run.is_directory()||run.is_symlink())
    throw std::runtime_error("Unexpected recovery run entry");
   for(const auto& side:fs::directory_iterator(run.path())){
    if(!side.is_directory()||side.is_symlink()||
       (side.path().filename()!="left"&&side.path().filename()!="right"))
     throw std::runtime_error("Unexpected recovery side entry");
    auto items=one_side(side.path());
    found.insert(found.end(),items.begin(),items.end());
    if(found.size()>100000)throw std::runtime_error("Too many recovery records");
   }
  }
 }
 return found;
}
fs::path RecoveryCatalog::export_item(const RecoveryItem& item,const fs::path& destination_root){
 validate_name(item.relative);
 no_symlinks(item.archive_side);
 no_symlinks(destination_root);
 if(!fs::is_directory(destination_root))
  throw std::runtime_error("Export destination must be an existing directory");
 const auto archive=canonical_endpoint_path(item.archive_side);
 const auto dest=canonical_endpoint_path(destination_root);
 auto path_under=[](const fs::path& a,const fs::path& b){
  auto i=a.begin(),j=b.begin();
  while(i!=a.end()&&j!=b.end()&&*i==*j){++i;++j;}
  return i==a.end();
 };
 if(path_under(archive,dest))
  throw std::runtime_error("Cannot export inside the archive or its parent");
 const auto source=item.archive_side/"data"/from_utf8(item.relative);
 auto target=dest/from_utf8(item.relative);
 no_symlinks(source);no_symlinks(target);
 if(fs::exists(target))throw std::runtime_error("Export target already exists; no overwrite permitted");
 // Do not trust UI state; re-parse the manifest and match all attributes.
 auto records=one_side(item.archive_side);
 auto match=std::find_if(records.begin(),records.end(),[&](const RecoveryItem& rec){
  return rec.relative==item.relative;
 });
 if(match==records.end()||match->hash!=item.hash||match->size!=item.size||
    match->directory!=item.directory||match->endpoint!=item.endpoint)
  throw std::runtime_error("Recovery item differs from its manifest");
 if(item.directory){
  if(!fs::is_directory(source))throw std::runtime_error("Archived directory missing");
  fs::create_directories(target);
  return target;
 }
 if(!fs::is_regular_file(source)||fs::file_size(source)!=item.size)
  throw std::runtime_error("Archived file missing or wrong size");
 {
  std::ifstream in(source,std::ios::binary);
  if(!in||sha256(in,item.size)!=item.hash)
   throw std::runtime_error("Archive checksum differs from manifest; export cancelled");
 }
 fs::create_directories(target.parent_path());
 no_symlinks(target);
 if(!fs::copy_file(source,target,fs::copy_options::none))
  throw std::runtime_error("Recovery export refused to overwrite an existing file");
 std::ifstream check(target,std::ios::binary);
 if(!check||sha256(check,item.size)!=item.hash){
  std::error_code ignored;
  fs::remove(target,ignored);
  throw std::runtime_error("Export integrity check failed");
 }
 return target;
}
}
