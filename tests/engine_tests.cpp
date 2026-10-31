#include "synchrorep/engine.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace synchrorep;
static void write(const fs::path& p,const std::string& text){
 fs::create_directories(p.parent_path());
 std::ofstream out(p,std::ios::binary|std::ios::trunc);
 out<<text;
}
static std::string read(const fs::path& p){
 std::ifstream f(p,std::ios::binary);
 return {std::istreambuf_iterator<char>(f),{}};
}
static bool throws(const auto& callable){try{callable();return false;}catch(const std::exception&){return true;}}
int main(){
 const auto root=fs::temp_directory_path()/("synchrorep-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 auto a=root/"a",b=root/"b",c=root/"c";fs::create_directories(a);fs::create_directories(b);fs::create_directories(c);
 try {
  Engine e(root/"state"/"history.txt");
  write(a/"file.txt","a");assert(e.run(a,b).copied==1);
  assert(read(b/"file.txt")=="a");assert(e.run(a,b).copied==0);
  write(b/"only-right.txt","right");assert(e.run(a,b).copied==1);
  assert(read(a/"only-right.txt")=="right");
  fs::remove(a/"file.txt");assert(e.run(a,b).removed==1);assert(!fs::exists(b/"file.txt"));
  write(a/"conflict.txt","0");assert(e.run(a,b).copied==1);
  write(a/"conflict.txt","LEFT");write(b/"conflict.txt","RIGHT");
  assert(e.run(a,b).conflicts.size()==1);
  assert(read(a/"conflict.txt")=="LEFT"&&read(b/"conflict.txt")=="RIGHT");
  // Preserve original state for skipped conflicts rather than learning changes.
  Engine ignoring(root/"state"/"history.txt",{ConflictPolicy::Ignore,true});
  write(a/"safe.txt","SAFE");
  auto partial=ignoring.run(a,b);
  assert(partial.conflicts.size()==1&&read(b/"safe.txt")=="SAFE");
  assert(ignoring.run(a,b).conflicts.size()==1);
  assert(throws([&]{e.run(a,c);}));
  assert(throws([&]{e.run(a,a);}));
  assert(throws([&]{e.run(a,a/"child");}));
  assert(throws([&]{Engine(a/"state.txt").run(a,b);}));
  assert(throws([&]{open_endpoint("sftp://example.org/home");}));
  assert(endpoint_kind("ftp://host/path")==EndpointKind::Ftp);
  assert(endpoint_kind("sftp://host/path")==EndpointKind::Sftp);
  assert(endpoint_kind("wsl://Ubuntu/home/user")==EndpointKind::Wsl);
  assert(endpoint_kind("C:/dev")==EndpointKind::Local);
  assert(throws([&]{wsl_unc_path("wsl://Ubuntu/../../Windows");}));
  assert(throws([&]{wsl_unc_path("wsl://bad%host/home");}));
  assert(throws([&]{open_endpoint("http://example.com");}));
  // Both files present before the first run: identical content is safe.
  auto x=root/"x",y=root/"y";fs::create_directories(x);fs::create_directories(y);
  write(x/"same.txt","MATCH");write(y/"same.txt","MATCH");
  Engine fresh(root/"state"/"fresh.txt");
  assert(fresh.run(x,y).conflicts.empty());
  write(x/"same.txt","DIFFERENT-LEFT");write(y/"same.txt","OTHER-RIGHT");
  assert(fresh.run(x,y).conflicts.size()==1);
  // One-sided deletion is propagated, but not when the other side changed.
  auto p=root/"p",q=root/"q";fs::create_directories(p);fs::create_directories(q);
  Engine deletion(root/"state"/"deletion.txt");
  write(p/"gone.txt","hello");deletion.run(p,q);fs::remove(p/"gone.txt");
  write(q/"gone.txt","updated on other side");
  assert(deletion.run(p,q).conflicts.size()==1);
  assert(fs::exists(q/"gone.txt"));
  // Safer policy: restore a deleted file when propagation is switched off.
  Engine restore(root/"state"/"restore.txt",{ConflictPolicy::Ask,false});
  write(p/"restore.txt","hello");restore.run(p,q);fs::remove(p/"restore.txt");
  assert(restore.run(p,q).copied==1);assert(read(p/"restore.txt")=="hello");
  // Files in nested directories and directory timestamp changes.
  write(p/"nested"/"a.txt","nested");assert(restore.run(p,q).conflicts.empty());
  assert(read(q/"nested"/"a.txt")=="nested");
  // Dry-run never creates a history snapshot or copies anything.
  auto u=root/"u",v=root/"v";fs::create_directories(u);fs::create_directories(v);
  write(u/"dry.txt","NEW");
  Engine dry(root/"state"/"dry.state");
  assert(dry.run(u,v,true).copied==0);
  assert(!fs::exists(v/"dry.txt"));
  assert(!fs::exists(root/"state"/"dry.state"));
  assert(dry.run(u,v).copied==1);
  // Newest and Oldest follow the original behavior enum for bilateral changes.
  auto n=root/"n",o=root/"o";fs::create_directories(n);fs::create_directories(o);
  write(n/"pick.txt","BASE");
  Engine newest(root/"state"/"newest.state",{ConflictPolicy::Newest,true});
  assert(newest.run(n,o).copied==1);
  write(n/"pick.txt","LEFT-CHANGED");write(o/"pick.txt","RIGHT-CHANGED");
  const auto t=fs::last_write_time(n/"pick.txt");
  fs::last_write_time(o/"pick.txt",t+std::chrono::hours(1));
  assert(newest.run(n,o).copied==1);
  assert(read(n/"pick.txt")=="RIGHT-CHANGED");
  write(n/"pick.txt","LEFT-AGAIN");write(o/"pick.txt","RIGHT-AGAIN");
  fs::last_write_time(o/"pick.txt",fs::last_write_time(n/"pick.txt")+std::chrono::hours(1));
  Engine oldest(root/"state"/"newest.state",{ConflictPolicy::Oldest,true});
  assert(oldest.run(n,o).copied==1);
  assert(read(o/"pick.txt")=="LEFT-AGAIN");
  // A malformed history or a symlink in the tree must fail closed.
  write(root/"state"/"bad.state","INVALID-HEADER\n");
  assert(throws([&]{Engine(root/"state"/"bad.state").run(u,v);}));
#ifndef _WIN32
  fs::create_symlink(u/"dry.txt",v/"link.txt");
  assert(throws([&]{dry.run(u,v);}));
  fs::remove(v/"link.txt");
#endif
  fs::remove_all(root);
  std::cout<<"Engine regression suite: OK\n";
 }catch(...){fs::remove_all(root);throw;}
}
