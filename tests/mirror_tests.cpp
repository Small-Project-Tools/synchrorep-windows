// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/engine.hpp"
#include "synchrorep/mirror_monitor.hpp"
#include <cassert>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace synchrorep;
namespace {
void write(const fs::path& path,const std::string& text){
 fs::create_directories(path.parent_path());
 std::ofstream out(path,std::ios::binary|std::ios::trunc);
 out<<text;
}
void write_atomic(const fs::path& path,const std::string& text){
 // Model an editor's atomic-save operation. In-place truncation may race
 // with a simultaneous SHA-256 scan and correctly stop the mirror.
 // Keep temporary editor files OUTSIDE the source being mirrored.
 // Otherwise monitoring may see transient files as real user data.
 auto temporary=fs::temp_directory_path()/(
   "synchrorep-editor-"+std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count())+".tmp");
 write(temporary,text);
#ifdef _WIN32
 // Windows rename() cannot replace an existing destination. Exercise the
 // same atomic replacement contract that real Windows editors provide.
 for(int attempt=0;attempt<50;++attempt){
  if(ReplaceFileW(path.c_str(),temporary.c_str(),nullptr,0,nullptr,nullptr))return;
  const auto error=GetLastError();
  if(error!=ERROR_SHARING_VIOLATION && error!=ERROR_ACCESS_DENIED)
   throw std::runtime_error("Windows atomic editor replace failed: "+std::to_string(error));
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }
 throw std::runtime_error("Windows atomic editor replace remained locked");
#else
 fs::rename(temporary,path);
#endif
}
std::string read(const fs::path& path){
 std::ifstream in(path,std::ios::binary);
 return {std::istreambuf_iterator<char>(in),{}};
}
bool throws(const auto& callback){
 try{callback();return false;}catch(const std::exception&){return true;}
}
void await(const auto& condition){
 // A shared CI runner may be pre-empted for substantial periods. Time out
 // by elapsed wall clock rather than a fixed number of polling iterations.
 static int waitNumber=0;
 const int step=++waitNumber;
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
 while(std::chrono::steady_clock::now()<deadline){
  if(condition())return;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
 }
 throw std::runtime_error("Mirror monitor did not react at await step "+std::to_string(step));
}
}
int main(){
 auto root=fs::temp_directory_path()/("synchrorep-mirror-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 auto src=root/"source",dst=root/"target",history=root/"state"/"mirror.txt";
 fs::create_directories(src);fs::create_directories(dst);
 try{
  Options opts;
  opts.mode=SyncMode::Mirror;
  opts.verify_hash=true;
  opts.recycle_local_deletions=false;
  Engine mirror(history,opts);
  // Mirror is not two-way: target extras are purged, not copied back.
  write(src/"only-source.txt","S1");
  write(dst/"foreign.txt","TARGET EXTRA");
  auto p=mirror.plan(src,dst);
  assert(p.size()==2);
  auto first=mirror.run(src,dst);
  assert(first.copied==1&&first.removed==1&&first.archived==1);
  assert(!fs::exists(src/"foreign.txt")&&!fs::exists(dst/"foreign.txt"));
  assert(read(dst/"only-source.txt")=="S1");
  const auto firstLog=root/"state"/"logs"/"mirror.txt.jsonl";
  const auto stableLogSize=fs::file_size(firstLog);
  assert(mirror.run(src,dst).copied==0);
  assert(fs::file_size(firstLog)==stableLogSize);
  // Target drift is healed; local source stays unchanged.
  auto t=fs::last_write_time(dst/"only-source.txt");
  write(dst/"only-source.txt","T1");
  fs::last_write_time(dst/"only-source.txt",t);
  auto healed=mirror.run(src,dst);
  assert(healed.copied==1&&healed.archived==1);
  assert(read(src/"only-source.txt")=="S1");
  assert(read(dst/"only-source.txt")=="S1");
  // Nested rename/edit and deletions propagate in the right direction.
  write(src/"folder"/"nested.txt","contents");
  assert(mirror.run(src,dst).copied==2);
  fs::rename(src/"folder"/"nested.txt",src/"folder"/"renamed.txt");
  auto renamed=mirror.run(src,dst);
  assert(renamed.removed==1&&renamed.copied==1);
  assert(!fs::exists(dst/"folder"/"nested.txt"));
  assert(read(dst/"folder"/"renamed.txt")=="contents");
  // An intentionally cleared source can remove last target after history.
  fs::remove_all(src/"folder");
  fs::remove(src/"only-source.txt");
  auto clear=mirror.run(src,dst);
  assert(clear.removed>=3);
  assert(fs::is_empty(dst));
  // Uninitialized, empty source must not erase an existing target.
  auto emptySrc=root/"empty",fullDst=root/"full";
  fs::create_directories(emptySrc);fs::create_directories(fullDst);
  write(fullDst/"important.txt","DO NOT ERASE");
  Engine initial(root/"state"/"new-mirror",opts);
  assert(throws([&]{initial.plan(emptySrc,fullDst);}));
  assert(throws([&]{initial.run(emptySrc,fullDst);}));
  assert(read(fullDst/"important.txt")=="DO NOT ERASE");
  // Empty-source guard doesn't alter an unrelated run's history.
  assert(!initial.has_pending_recovery());
  // File/dir type clash blocks all operations with no source mutation.
  auto typeSrc=root/"type-src",typeDst=root/"type-dst";
  fs::create_directories(typeSrc);fs::create_directories(typeDst);
  write(typeSrc/"one","FILE");
  fs::create_directories(typeDst/"one");
  Engine clash(root/"state"/"clash",opts);
  assert(clash.run(typeSrc,typeDst).conflicts.size()==1);
  assert(fs::is_directory(typeDst/"one"));
  assert(read(typeSrc/"one")=="FILE");
  Options ignored=opts;ignored.conflict=ConflictPolicy::Ignore;
  Engine ignoredClash(root/"state"/"ignored-clash",ignored);
  assert(ignoredClash.run(typeSrc,typeDst).conflicts.size()==1);
  assert(fs::is_directory(typeDst/"one"));
  // No deletion policy means stray targets are retained but not propagated.
  Options keep;keep.mode=SyncMode::Mirror;keep.propagate_deletions=false;keep.recycle_local_deletions=false;
  Engine noDelete(root/"state"/"no-deletions",keep);
  write(typeDst/"extra","unrelated");
  assert(noDelete.plan(typeSrc,typeDst).size()==2);
  assert(!fs::exists(typeSrc/"extra"));
  // Without deletion propagation, target-only files are preserved
  // but must never appear on the source.
  auto keepSrc=root/"keep-src",keepDst=root/"keep-dst";
  fs::create_directories(keepSrc);fs::create_directories(keepDst);
  write(keepSrc/"canonical","SOURCE");
  write(keepDst/"extra","TARGET");
  Engine keepEngine(root/"state"/"keep",keep);
  auto kept=keepEngine.run(keepSrc,keepDst);
  assert(kept.copied==1&&kept.removed==0);
  assert(read(keepDst/"extra")=="TARGET");
  assert(!fs::exists(keepSrc/"extra"));
  // A missing/disconnected source is never interpreted as deletion.
  auto disconnectedSrc=root/"disconnect-src",disconnectedDst=root/"disconnect-dst";
  fs::create_directories(disconnectedSrc);fs::create_directories(disconnectedDst);
  write(disconnectedSrc/"important","DATA");
  Engine disconnected(root/"state"/"disconnected",opts);
  LocalEndpoint disSource(disconnectedSrc),disTarget(disconnectedDst);
  MirrorMonitorSettings fast{std::chrono::milliseconds(120),std::chrono::seconds(1)};
  MirrorMonitor disMonitor(fast);
  std::atomic<bool> disStop=false;
  RunCallbacks disCallbacks;
  disCallbacks.cancelled=[&]{return disStop.load();};
  auto disFuture=std::async(std::launch::async,[&]{
   return disMonitor.run(disSource,disTarget,disconnected,disconnectedSrc,disCallbacks);
  });
  try{
   await([&]{return fs::exists(disconnectedDst/"important");});
   fs::remove_all(disconnectedSrc);
   assert(disFuture.wait_for(std::chrono::seconds(15))==std::future_status::ready);
   bool errored=false;
   try{(void)disFuture.get();}catch(const std::exception&){errored=true;}
   assert(errored);
   assert(read(disconnectedDst/"important")=="DATA");
  }catch(...){disStop=true;disFuture.wait();throw;}
  // Continuous monitor reacts to edits, deletions and remote drift.
  auto watchSrc=root/"watch-src",watchDst=root/"watch-dst";
  fs::create_directories(watchSrc);fs::create_directories(watchDst);
  write(watchSrc/"a","alpha");
  Engine watchEngine(root/"state"/"watch",opts);
  LocalEndpoint a(watchSrc),b(watchDst);
  std::atomic<bool> stop=false;
  std::atomic<bool> stopOnNextIdle=false;
  RunCallbacks callbacks;
  callbacks.cancelled=[&]{return stop.load();};
  callbacks.progress=[&](const Progress& event){
   if(event.phase==Phase::Watch && stopOnNextIdle.load())stop.store(true);
  };
  MirrorMonitorSettings settings{std::chrono::milliseconds(120),std::chrono::seconds(1)};
  MirrorMonitor monitor(settings);
  auto result=std::async(std::launch::async,[&]{
   return monitor.run(a,b,watchEngine,watchSrc,callbacks);
  });
  try{
   await([&]{return fs::exists(watchDst/"a")&&read(watchDst/"a")=="alpha";});
   write_atomic(watchSrc/"a","beta");
   await([&]{return read(watchDst/"a")=="beta";});
   write_atomic(watchDst/"a","remote edit");
   await([&]{return read(watchDst/"a")=="beta";});
   auto oldTime=fs::last_write_time(watchSrc/"a");
   write_atomic(watchSrc/"a","zeta");
   fs::last_write_time(watchSrc/"a",oldTime);
   await([&]{return read(watchDst/"a")=="zeta";});
   write(watchSrc/"new-folder"/"n","nested");
   await([&]{return fs::exists(watchDst/"new-folder"/"n")&&read(watchDst/"new-folder"/"n")=="nested";});
   fs::remove(watchSrc/"a");
   await([&]{return !fs::exists(watchDst/"a");});
  }catch(...){stop=true;result.wait();throw;}
  // Stop at a confirmed idle boundary, not in the middle of a transfer.
  stopOnNextIdle=true;
  await([&]{return stop.load();});
  assert(result.wait_for(std::chrono::seconds(15))==std::future_status::ready);
  auto totals=result.get();
  assert(totals.copied>=3&&totals.removed>=1);
  assert(read(watchSrc/"new-folder"/"n")=="nested");
  assert(!watchEngine.has_pending_recovery());
  fs::remove_all(root);
  std::cout<<"Mirror one-way and continuous monitoring passed\n";
 }catch(...){fs::remove_all(root);throw;}
}
