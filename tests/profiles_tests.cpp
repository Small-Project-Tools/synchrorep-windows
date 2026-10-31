#include "synchrorep/profiles.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace synchrorep;
int main(){
 const auto dir=fs::temp_directory_path()/("synchrorep-profiles-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(dir);
 try {
  ProfileStore store(dir/"profiles.dat");
  assert(store.load().empty());
  Profile p{"L\xC3\xA9" "a", "C:/un dossier", "sftp://127.0.0.1:22/a b/", "C:/Users/test/.ssh/known_hosts", ConflictPolicy::Newest, false};
  p.preserve_changes=false;
  p.recycle_local_deletions=false;
  store.save({p});
  auto a=store.load();assert(a.size()==1);
  assert(a.at(0).name==p.name&&a.at(0).left==p.left&&a.at(0).right==p.right);
  assert(a.at(0).conflict==ConflictPolicy::Newest&&!a.at(0).propagate_deletions);
  assert(!a.at(0).preserve_changes);
  assert(!a.at(0).recycle_local_deletions);
  // V1 profiles migrate automatically: preserved copies are enabled by default.
  std::ofstream(dir/"profiles.dat",std::ios::trunc)
   <<"SYNCHROREP-PROFILES1\n"
   <<"\"legacy\" \"C:/left\" \"C:/right\" \"\" 0 1\n";
  auto legacy=store.load();
  assert(legacy.size()==1&&legacy.at(0).preserve_changes&&legacy.at(0).recycle_local_deletions);
  std::ofstream(dir/"profiles.dat",std::ios::trunc)
   <<"SYNCHROREP-PROFILES2\n"
   <<"\"v2\" \"C:/left\" \"C:/right\" \"\" 0 1 0\n";
  auto second=store.load();
  assert(second.size()==1&&!second.at(0).preserve_changes&&second.at(0).recycle_local_deletions);
  store.save({p});
  // A malformed V3 recycle policy must be rejected.
  std::ofstream(dir/"profiles.dat",std::ios::trunc)
   <<"SYNCHROREP-PROFILES3\n"
   <<"\"bad\" \"C:/left\" \"C:/right\" \"\" 0 1 1 7\n";
  bool invalid=false;try{store.load();}catch(const std::runtime_error&){invalid=true;}
  assert(invalid);
  store.save({p});
  bool duplicate=false;
  try{store.save({p,p});}catch(const std::invalid_argument&){duplicate=true;}
  assert(duplicate);
  std::ofstream(dir/"profiles.dat",std::ios::trunc)<<"SYNCHROREP-PROFILES1\n\"truncated\"";
  bool corrupted=false;try{store.load();}catch(const std::runtime_error&){corrupted=true;}
  assert(corrupted);
  fs::remove_all(dir);std::cout<<"Profiles roundtrip and validation passed\n";
 }catch(...){fs::remove_all(dir);throw;}
}
