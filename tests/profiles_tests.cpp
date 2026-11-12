// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/profiles.hpp"
#include <stdexcept>
#define assert(expression) do { \
 if (!(expression)) throw std::runtime_error("Assertion failed at line " + \
 std::to_string(__LINE__) + ": " #expression); \
} while (false)
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
  p.mode=SyncMode::Mirror;
  p.auto_start=true;
  p.verify_hash=true;
  p.resume_interrupted=false;
  p.exclude_patterns={"*.tmp","cache/**","**/*.log"};
  store.save({p});
  auto a=store.load();assert(a.size()==1);
  assert(a.at(0).name==p.name&&a.at(0).left==p.left&&a.at(0).right==p.right);
  assert(a.at(0).conflict==ConflictPolicy::Newest&&!a.at(0).propagate_deletions);
  assert(!a.at(0).preserve_changes);
  assert(!a.at(0).recycle_local_deletions);
  assert(a.at(0).mode==SyncMode::Mirror&&a.at(0).auto_start&&a.at(0).verify_hash);
  assert(!a.at(0).resume_interrupted&&a.at(0).exclude_patterns==p.exclude_patterns);
  // V1 profiles migrate automatically: preserved copies are enabled by default.
  std::ofstream(dir/"profiles.dat",std::ios::trunc)
   <<"SYNCHROREP-PROFILES1\n"
   <<"\"legacy\" \"C:/left\" \"C:/right\" \"\" 0 1\n";
  auto legacy=store.load();
  assert(legacy.size()==1&&legacy.at(0).resume_interrupted&&!legacy.at(0).verify_hash&&!legacy.at(0).auto_start&&legacy.at(0).preserve_changes&&legacy.at(0).recycle_local_deletions&&legacy.at(0).mode==SyncMode::Bidirectional);
  std::ofstream(dir/"profiles.dat",std::ios::trunc)
   <<"SYNCHROREP-PROFILES2\n"
   <<"\"v2\" \"C:/left\" \"C:/right\" \"\" 0 1 0\n";
  auto second=store.load();
  assert(second.size()==1&&!second.at(0).verify_hash&&!second.at(0).auto_start&&!second.at(0).preserve_changes&&second.at(0).recycle_local_deletions&&second.at(0).mode==SyncMode::Bidirectional);
  store.save({p});
  // A malformed V3 recycle policy must be rejected.
  std::ofstream(dir/"profiles.dat",std::ios::trunc)
   <<"SYNCHROREP-PROFILES3\n"
   <<"\"bad\" \"C:/left\" \"C:/right\" \"\" 0 1 1 7\n";
  bool invalid=false;try{store.load();}catch(const std::runtime_error&){invalid=true;}
  assert(invalid);
  std::ofstream(dir/"profiles.dat",std::ios::trunc)
   <<"SYNCHROREP-PROFILES3\n"
   <<"\"v3\" \"C:/left\" \"C:/right\" \"\" 0 1 1 1\n";
  assert(store.load().at(0).mode==SyncMode::Bidirectional);
  assert(!store.load().at(0).auto_start);
  std::ofstream(dir/"profiles.dat",std::ios::trunc)
   <<"SYNCHROREP-PROFILES4\n"
   <<"\"v4\" \"C:/left\" \"C:/right\" \"\" 0 1 1 1 1\n";
  assert(store.load().at(0).mode==SyncMode::Mirror);
  assert(!store.load().at(0).auto_start);
  store.save({p});
  Profile invalidStartup=p;
  invalidStartup.mode=SyncMode::Bidirectional;
  assert([&]{try{store.save({invalidStartup});}catch(const std::invalid_argument&){return true;}return false;}());
  std::ofstream(dir/"profiles.dat",std::ios::trunc)
   <<"SYNCHROREP-PROFILES5\n"
   <<"\"bad\" \"C:/left\" \"C:/right\" \"\" 0 1 1 1 0 1\n";
  bool rejected=false;
  try{(void)store.load();}catch(const std::runtime_error&){rejected=true;}
  assert(rejected);
  store.save({p});
  // Do not persist two auto-start mirrors that can erase each other's target.
  Profile secondMirror=p;
  secondMirror.name="other";
  secondMirror.left="D:/other";
  secondMirror.right="sftp://127.0.0.1:22/a b/subfolder";
  assert([&]{
   try{store.save({p,secondMirror});}
   catch(const std::invalid_argument&){return true;}
   return false;
  }());
  assert(store.load().size()==1);
  bool duplicate=false;
  try{store.save({p,p});}catch(const std::invalid_argument&){duplicate=true;}
  assert(duplicate);
  std::ofstream(dir/"profiles.dat",std::ios::trunc)<<"SYNCHROREP-PROFILES1\n\"truncated\"";
  bool corrupted=false;try{store.load();}catch(const std::runtime_error&){corrupted=true;}
  assert(corrupted);
  fs::remove_all(dir);std::cout<<"Profiles roundtrip and validation passed\n";
 }catch(...){fs::remove_all(dir);throw;}
}
