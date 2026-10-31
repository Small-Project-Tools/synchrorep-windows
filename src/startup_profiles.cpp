#include "synchrorep/startup_profiles.hpp"
#include "synchrorep/sha256.hpp"
#include <stdexcept>
#include <set>

namespace synchrorep {
std::vector<StartupEntry> startup_entries(const std::vector<Profile>& profiles){
 std::vector<StartupEntry> entries;
 std::set<std::string> names,keys;
 for(const auto& profile:profiles){
  if(profile.name.empty()||profile.name.size()>500||
     profile.name.find_first_of("\x00\r\n",0,3)!=std::string::npos||
     !names.insert(profile.name).second)
   throw std::invalid_argument("Duplicate or empty profile name");
  if(!profile.auto_start)continue;
  if(profile.mode!=SyncMode::Mirror||profile.left.empty()||profile.right.empty())
   throw std::invalid_argument("Only complete mirror profiles may start with Windows");
  Sha256 hash;
  hash.update(reinterpret_cast<const unsigned char*>(profile.name.data()),profile.name.size());
  const auto key="SynchrorepMirror-"+hash.finish();
  if(!keys.insert(key).second)throw std::invalid_argument("Duplicate startup value");
  entries.push_back({key,profile.name});
 }
 return entries;
}
}
