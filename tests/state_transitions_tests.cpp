#include "synchrorep/engine.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
using namespace synchrorep;
static void write(const fs::path& path,const std::string& value){
 fs::create_directories(path.parent_path());
 std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<value;
}
static Action decision(Engine& engine,const fs::path& left,const fs::path& right){
 const auto choices=engine.plan(left,right);
 for(const auto& item:choices)if(item.relative=="document.txt")return item.action;
 return Action::None;
}
int main(){
 auto base=fs::temp_directory_path()/("synchrorep-transitions-"+std::to_string(
   std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  auto l=base/"left",r=base/"right";fs::create_directories(l);fs::create_directories(r);
  Options safe;
  safe.recycle_local_deletions=false; // Native Recycle Bin needs an interactive Windows Shell.
  Engine engine(base/"history.state",safe);
  write(l/"document.txt","initial");
  assert(decision(engine,l,r)==Action::CopyLeftToRight);
  assert(engine.run(l,r).copied==1);
  assert(decision(engine,l,r)==Action::None);
  const auto original=fs::last_write_time(l/"document.txt");
  write(l/"document.txt","updated");
  fs::last_write_time(l/"document.txt",original+std::chrono::seconds(10));
  assert(decision(engine,l,r)==Action::CopyLeftToRight);
  assert(engine.run(l,r).copied==1);
  assert(decision(engine,l,r)==Action::None);
  write(r/"document.txt","newer");
  fs::last_write_time(r/"document.txt",original+std::chrono::seconds(20));
  assert(decision(engine,l,r)==Action::CopyRightToLeft);
  assert(engine.run(l,r).copied==1);
  fs::remove(l/"document.txt");
  assert(decision(engine,l,r)==Action::DeleteRight);
  assert(engine.run(l,r).removed==1);
  assert(decision(engine,l,r)==Action::None);
  write(r/"document.txt","recreated");
  assert(decision(engine,l,r)==Action::CopyRightToLeft);
  assert(engine.run(l,r).copied==1);
  // Simultaneous equal-time divergent edits are conflicts when content hashes
  // establish an actual difference.
  Options hash;hash.verify_hash=true;hash.recycle_local_deletions=false;
  Engine verified(base/"history.state",hash);
  auto current=fs::last_write_time(l/"document.txt");
  write(l/"document.txt","1111");
  write(r/"document.txt","2222");
  fs::last_write_time(l/"document.txt",current+std::chrono::seconds(40));
  fs::last_write_time(r/"document.txt",current+std::chrono::seconds(40));
  assert(decision(verified,l,r)==Action::Conflict);
  assert(verified.run(l,r).conflicts.size()==1);
  fs::remove_all(base);
  std::cout<<"State transition regression matrix OK\n";
 }catch(...){fs::remove_all(base);throw;}
}
