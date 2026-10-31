#include "synchrorep/explorer_launch.hpp"
#include "synchrorep/startup_profiles.hpp"
#include "synchrorep/endpoint.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace synchrorep {
namespace {
std::string canonical_folder(const std::string& path) {
 if(endpoint_kind(path)!=EndpointKind::Local)
  throw std::invalid_argument("Explorer synchronization requires local or UNC folders");
 return canonical_mirror_target(path);
}
std::string folder_name(std::string path) {
 std::replace(path.begin(),path.end(),'\\','/');
 while(path.size()>1&&path.back()=='/')path.pop_back();
 const auto slash=path.find_last_of('/');
 const std::string name=slash==std::string::npos?path:path.substr(slash+1);
 return name.empty()?"Folder":name;
}
}
int explorer_profile_index(const std::vector<Profile>& profiles,const std::string& folder){
 const auto key=canonical_folder(folder);
 for(std::size_t i=0;i<profiles.size();++i){
  for(const auto& endpoint:{profiles[i].left,profiles[i].right}){
   try{
    if(canonical_folder(endpoint)==key)return static_cast<int>(i);
   }catch(const std::invalid_argument&){
    // FTP/SFTP endpoints or legacy relative paths are not local Explorer folders.
   }
  }
 }
 return -1;
}
Profile explorer_new_profile(const std::vector<Profile>& profiles,
                             const std::string& source,
                             const std::string& destination){
 (void)canonical_folder(source);
 (void)canonical_folder(destination);
 if(mirror_targets_overlap(source,destination))
  throw std::invalid_argument("Source and destination cannot be the same or nested folders");
 if(explorer_profile_index(profiles,source)>=0 ||
    explorer_profile_index(profiles,destination)>=0)
  throw std::invalid_argument("One of these folders already belongs to a synchronization profile");
 Profile p;
 p.left=source;
 p.right=destination;
 p.mode=SyncMode::Bidirectional;
 p.conflict=ConflictPolicy::Ask;
 p.propagate_deletions=true;
 p.preserve_changes=true;
 p.recycle_local_deletions=true;
 p.auto_start=false;
 const auto label=folder_name(source)+" - "+folder_name(destination);
 p.name=label;
 for(int suffix=2;;++suffix){
  const bool exists=std::any_of(profiles.begin(),profiles.end(),
                 [&](const Profile& item){return item.name==p.name;});
  if(!exists)break;
  p.name=label+" ("+std::to_string(suffix)+")";
 }
 return p;
}
}
