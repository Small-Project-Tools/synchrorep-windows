#include "synchrorep/profiles.hpp"
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <set>
namespace synchrorep {
std::vector<Profile> ProfileStore::load() const {
 if(!fs::exists(file_))return {};
 std::ifstream in(file_,std::ios::binary);
 if(!in)throw std::runtime_error("Cannot read Synchrorep profiles");
 std::string header;std::getline(in,header);
 const bool previous=header=="SYNCHROREP-PROFILES1";
 const bool second=header=="SYNCHROREP-PROFILES2";
 if(!previous&&!second&&header!="SYNCHROREP-PROFILES3")
  throw std::runtime_error("Unsupported profiles file");
 std::vector<Profile> profiles;std::set<std::string> names;
 while(in>>std::ws&&!in.eof()){
  Profile profile;int policy=0,deletions=0,preserve=1,recycle=1;
  if(!(in>>std::quoted(profile.name)>>std::quoted(profile.left)>>std::quoted(profile.right)
     >>std::quoted(profile.known_hosts)>>policy>>deletions))
   throw std::runtime_error("Malformed profiles file");
  if(!previous && !(in>>preserve))throw std::runtime_error("Missing backup policy in profile");
  if(!previous&&!second && !(in>>recycle))throw std::runtime_error("Missing recycle policy in profile");
  if(profile.name.empty()||profile.left.empty()||profile.right.empty()||
      profile.name.size()>500||policy<0||policy>4||(deletions!=0&&deletions!=1)||(preserve!=0&&preserve!=1)||(recycle!=0&&recycle!=1)||
      !names.insert(profile.name).second)
   throw std::runtime_error("Invalid or duplicate synchronization profile");
  profile.conflict=static_cast<ConflictPolicy>(policy);
  profile.propagate_deletions=deletions!=0;
  profile.preserve_changes=preserve!=0;
  profile.recycle_local_deletions=recycle!=0;
  profiles.push_back(std::move(profile));
  if(profiles.size()>1000)throw std::runtime_error("Too many profiles");
 }
 return profiles;
}
void ProfileStore::save(const std::vector<Profile>& profiles) const {
 if(profiles.size()>1000)throw std::invalid_argument("Too many profiles");
 std::set<std::string> names;
 for(const auto& p:profiles) {
  if(p.name.empty()||p.left.empty()||p.right.empty()||
     !names.insert(p.name).second)
   throw std::invalid_argument("Invalid or duplicate profile");
 }
 auto parent=file_.parent_path();
 if(!parent.empty())fs::create_directories(parent);
 auto tmp=file_;tmp+=".tmp";auto bak=file_;bak+=".bak";
 {
  std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
  if(!out)throw std::runtime_error("Cannot create profile file");
  out<<"SYNCHROREP-PROFILES3\n";
  for(const auto& p:profiles)
   out<<std::quoted(p.name)<<' '<<std::quoted(p.left)<<' '
      <<std::quoted(p.right)<<' '<<std::quoted(p.known_hosts)<<' '
      <<static_cast<int>(p.conflict)<<' '<<p.propagate_deletions<<' '
      <<p.preserve_changes<<' '<<p.recycle_local_deletions<<'\n';
  out.flush();if(!out)throw std::runtime_error("Cannot write profiles");
 }
 std::error_code ec;
 fs::rename(tmp,file_,ec);
 if(!ec)return;
 if(!fs::exists(file_))throw fs::filesystem_error("Cannot save profiles",tmp,file_,ec);
 fs::remove(bak,ec);
 fs::rename(file_,bak);
 try{fs::rename(tmp,file_);}catch(...){fs::rename(bak,file_);throw;}
 fs::remove(bak,ec);
}
}
