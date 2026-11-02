// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/engine.hpp"
#include "synchrorep/recycle_bin.hpp"
#include <cassert>
#include <algorithm>
#include <map>
#include <vector>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
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
// Most engine tests validate sync rules, not Windows Shell interactivity.
Options no_native_recycle(Options options={}){
 options.recycle_local_deletions=false;
 return options;
}
struct EngineWithoutNativeRecycle final:Engine {
 explicit EngineWithoutNativeRecycle(fs::path state,Options options={})
 :Engine(std::move(state),no_native_recycle(std::move(options))){}
};
// Injectable fake of the Windows Shell recycler: exercises engine policy
// without needing a GUI or an actual Windows Recycle Bin in CI.
struct TestRecyclingEndpoint final:Endpoint {
 LocalEndpoint inner;
 fs::path simulated_bin;
 bool fail=false;
 int recycled=0;
 int permanently_deleted=0;
 TestRecyclingEndpoint(const fs::path& path,const fs::path& bin)
 :inner(path),simulated_bin(bin){}
 std::string identity()const override {return inner.identity();}
 Snapshot scan()const override {return inner.scan();}
 std::unique_ptr<std::istream> read(const std::string& relative)const override {return inner.read(relative);}
 void write(const std::string& relative,std::istream& input,const Entry& data)override {
  inner.write(relative,input,data);
 }
 void make_directory(const std::string& relative)override {inner.make_directory(relative);}
 void remove(const std::string& relative)override {
  ++permanently_deleted;
  inner.remove(relative);
 }
 bool supports_recycle_bin()const override{return true;}
 void recycle(const std::string& relative)override {
  if(fail)throw std::runtime_error("Simulated recycle operation failed");
  fs::create_directories((simulated_bin/relative).parent_path());
  fs::rename(inner.root()/relative,simulated_bin/relative);
  ++recycled;
 }
};
int main(){
 const auto root=fs::temp_directory_path()/("synchrorep-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 auto a=root/"a",b=root/"b",c=root/"c";fs::create_directories(a);fs::create_directories(b);fs::create_directories(c);
 try {
  EngineWithoutNativeRecycle e(root/"state"/"history.txt",Options{ConflictPolicy::Ask,true,{},false,true,false});
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
  EngineWithoutNativeRecycle ignoring(root/"state"/"history.txt",{ConflictPolicy::Ignore,true});
  write(a/"safe.txt","SAFE");
  auto partial=ignoring.run(a,b);
  assert(partial.conflicts.size()==1&&read(b/"safe.txt")=="SAFE");
  assert(ignoring.run(a,b).conflicts.size()==1);
#ifdef SYNCHROREP_SQLITE_ENABLED
  // Persisted history is SQLite, and must reject reuse with other endpoints.
  assert(fs::exists(root/"state"/"history.txt.sqlite"));
  assert(!fs::exists(root/"state"/"history.txt")); // New configurations never emit text history.
#endif
  assert(throws([&]{e.run(a,c);}));
  assert(throws([&]{e.run(a,a);}));
  assert(throws([&]{e.run(a,a/"child");}));
  assert(throws([&]{EngineWithoutNativeRecycle(a/"state.txt").run(a,b);}));
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
  EngineWithoutNativeRecycle fresh(root/"state"/"fresh.txt");
  assert(fresh.run(x,y).conflicts.empty());
  write(x/"same.txt","DIFFERENT-LEFT");write(y/"same.txt","OTHER-RIGHT");
  assert(fresh.run(x,y).conflicts.size()==1);
  // One-sided deletion is propagated, but not when the other side changed.
  auto p=root/"p",q=root/"q";fs::create_directories(p);fs::create_directories(q);
  EngineWithoutNativeRecycle deletion(root/"state"/"deletion.txt");
  write(p/"gone.txt","hello");deletion.run(p,q);fs::remove(p/"gone.txt");
  write(q/"gone.txt","updated on other side");
  assert(deletion.run(p,q).conflicts.size()==1);
  assert(fs::exists(q/"gone.txt"));
  // Safer policy: restore a deleted file when propagation is switched off.
  EngineWithoutNativeRecycle restore(root/"state"/"restore.txt",{ConflictPolicy::Ask,false});
  write(p/"restore.txt","hello");restore.run(p,q);fs::remove(p/"restore.txt");
  assert(restore.run(p,q).copied==1);assert(read(p/"restore.txt")=="hello");
  // Files in nested directories and directory timestamp changes.
  write(p/"nested"/"a.txt","nested");assert(restore.run(p,q).conflicts.empty());
  assert(read(q/"nested"/"a.txt")=="nested");
  // Dry-run never creates a history snapshot or copies anything.
  auto u=root/"u",v=root/"v";fs::create_directories(u);fs::create_directories(v);
  write(u/"dry.txt","NEW");
  EngineWithoutNativeRecycle dry(root/"state"/"dry.state");
  assert(dry.run(u,v,true).copied==0);
  assert(!fs::exists(v/"dry.txt"));
  assert(!fs::exists(root/"state"/"dry.state"));
  assert(dry.run(u,v).copied==1);
  // Newest and Oldest follow the original behavior enum for bilateral changes.
  auto n=root/"n",o=root/"o";fs::create_directories(n);fs::create_directories(o);
  write(n/"pick.txt","BASE");
  EngineWithoutNativeRecycle newest(root/"state"/"newest.state",{ConflictPolicy::Newest,true});
  assert(newest.run(n,o).copied==1);
  write(n/"pick.txt","LEFT-CHANGED");write(o/"pick.txt","RIGHT-CHANGED");
  const auto t=fs::last_write_time(n/"pick.txt");
  fs::last_write_time(o/"pick.txt",t+std::chrono::hours(1));
  assert(newest.run(n,o).copied==1);
  assert(read(n/"pick.txt")=="RIGHT-CHANGED");
  write(n/"pick.txt","LEFT-AGAIN");write(o/"pick.txt","RIGHT-AGAIN");
  fs::last_write_time(o/"pick.txt",fs::last_write_time(n/"pick.txt")+std::chrono::hours(1));
  EngineWithoutNativeRecycle oldest(root/"state"/"newest.state",{ConflictPolicy::Oldest,true});
  assert(oldest.run(n,o).copied==1);
  assert(read(n/"pick.txt")=="RIGHT-AGAIN");
  // Same byte length AND restored timestamp must still be detected.
  auto hz=root/"hz",hz2=root/"hz2";fs::create_directories(hz);fs::create_directories(hz2);
  Options fingerprintOptions;fingerprintOptions.verify_hash=true;fingerprintOptions.recycle_local_deletions=false;
  EngineWithoutNativeRecycle fingerprint(root/"state"/"fingerprint.state",fingerprintOptions);
  write(hz/"touch.txt","AAAA");
  assert(fingerprint.run(hz,hz2).copied==1);
  auto oldtime=fs::last_write_time(hz/"touch.txt");
  write(hz/"touch.txt","BBBB");
  fs::last_write_time(hz/"touch.txt",oldtime);
  assert(fingerprint.plan(hz,hz2).at(0).action==Action::Conflict);
  assert(fingerprint.run(hz,hz2).conflicts.size()==1);
  assert(read(hz2/"touch.txt")=="AAAA");
  // When both ends diverge with identical size/timestamps, stop.
  write(hz/"touch.txt","CCCC");
  write(hz2/"touch.txt","DDDD");
  fs::last_write_time(hz/"touch.txt",oldtime);
  fs::last_write_time(hz2/"touch.txt",oldtime);
  assert(fingerprint.run(hz,hz2).conflicts.size()==1);
  assert(read(hz/"touch.txt")=="CCCC");
  assert(read(hz2/"touch.txt")=="DDDD");
  // Detect a changed file even when only its timestamp is restored,
  // preventing spurious delete-versus-update decisions.
  auto hx=root/"hx",hy=root/"hy";fs::create_directories(hx);fs::create_directories(hy);
  EngineWithoutNativeRecycle delhash(root/"state"/"delete-with-hash.state",fingerprintOptions);
  write(hx/"file.txt","1111");
  assert(delhash.run(hx,hy).copied==1);
  auto t_before=fs::last_write_time(hy/"file.txt");
  fs::remove(hx/"file.txt");
  write(hy/"file.txt","2222");
  fs::last_write_time(hy/"file.txt",t_before);
  assert(delhash.run(hx,hy).removed==1);
  assert(!fs::exists(hy/"file.txt"));
  // Protected overwrite stores the previous bytes before replacement.
  auto protectedLeft=root/"protected-left",protectedRight=root/"protected-right";
  fs::create_directories(protectedLeft);fs::create_directories(protectedRight);
  auto protectedState=root/"state"/"protected.state";
  EngineWithoutNativeRecycle protectedEngine(protectedState,fingerprintOptions);
  write(protectedLeft/"nested"/"data.txt","OLD-ORIGINAL");
  assert(protectedEngine.run(protectedLeft,protectedRight).copied>=1);
  write(protectedLeft/"nested"/"data.txt","NEW-CONTENTS");
  fs::last_write_time(protectedLeft/"nested"/"data.txt",
    fs::last_write_time(protectedRight/"nested"/"data.txt")+std::chrono::seconds(5));
  auto overwritten=protectedEngine.run(protectedLeft,protectedRight);
  assert(overwritten.copied==1 && overwritten.archived==1);
  assert(read(protectedRight/"nested"/"data.txt")=="NEW-CONTENTS");
  auto recoveries=protectedState.parent_path()/"recovery"/protectedState.filename();
  std::size_t backups=0;
  for(const auto& item:fs::recursive_directory_iterator(recoveries)){
   if(item.is_regular_file() && item.path().filename()=="data.txt"){
    assert(read(item.path())=="OLD-ORIGINAL");
    ++backups;
   }
  }
  assert(backups==1);
  // Removing a populated directory archives its file plus directory marker.
  fs::remove_all(protectedLeft/"nested");
  auto removed=protectedEngine.run(protectedLeft,protectedRight);
  assert(removed.removed==2 && removed.archived==2);
  assert(!fs::exists(protectedRight/"nested"));
  backups=0;
  for(const auto& item:fs::recursive_directory_iterator(recoveries))
   if(item.is_regular_file() && item.path().filename()=="data.txt"){
    const auto text=read(item.path());
    assert(text=="OLD-ORIGINAL" || text=="NEW-CONTENTS");
    ++backups;
   }
  assert(backups==2);
  const auto eventLog=protectedState.parent_path()/"logs"/"protected.state.jsonl";
  assert(fs::exists(eventLog));
  const auto content=read(eventLog);
  assert(content.find("\"event\":\"preserved\"")!=std::string::npos);
  assert(content.find("\"event\":\"committed\"")!=std::string::npos);
  // Recovery policy can be explicitly disabled: useful for disposable data.
  auto optLeft=root/"opt-left",optRight=root/"opt-right";
  fs::create_directories(optLeft);fs::create_directories(optRight);
  auto optState=root/"state"/"without-backups.state";
  EngineWithoutNativeRecycle unprotected(optState,Options{ConflictPolicy::Ask,true,{},false,false});
  write(optLeft/"record","before");
  unprotected.run(optLeft,optRight);
  write(optLeft/"record","after");
  auto fast=unprotected.run(optLeft,optRight);
  assert(fast.copied==1&&fast.archived==0);
  assert(!fs::exists(optState.parent_path()/"recovery"/optState.filename()));
  // Read-only scan never creates a recovery directory for its state.
  auto noArchiveState=root/"state"/"preview-only.state";
  EngineWithoutNativeRecycle noArchive(noArchiveState);
  assert(noArchive.plan(protectedLeft,protectedRight).empty());
  assert(!fs::exists(noArchiveState.parent_path()/"recovery"/noArchiveState.filename()));
  // Previous history formats cannot be silently mistaken for SHA-256 state.
  write(root/"state"/"legacy.state","SYNCHROREP3\\n");
  assert(throws([&]{EngineWithoutNativeRecycle(root/"state"/"legacy.state").plan(hz,hz2);}));
  // Resolve different simultaneous edits with explicit per-file choices.
  auto cl=root/"cl",cr=root/"cr";fs::create_directories(cl);fs::create_directories(cr);
  EngineWithoutNativeRecycle first(root/"state"/"resolution.state");
  write(cl/"left.txt","base");write(cl/"right.txt","base");
  write(cl/"skip.txt","base");write(cl/"deleted.txt","base");
  assert(first.run(cl,cr).copied==4);
  write(cl/"left.txt","LEFT");write(cr/"left.txt","RIGHT");
  write(cl/"right.txt","LEFT");write(cr/"right.txt","RIGHT");
  write(cl/"skip.txt","LEFT");write(cr/"skip.txt","RIGHT");
  fs::remove(cl/"deleted.txt");write(cr/"deleted.txt","changed");
  std::map<std::string,ConflictChoice> chosen{{"left.txt",ConflictChoice::KeepLeft},
   {"right.txt",ConflictChoice::KeepRight},{"skip.txt",ConflictChoice::Skip},
   {"deleted.txt",ConflictChoice::KeepLeft}};
  EngineWithoutNativeRecycle resolved(root/"state"/"resolution.state",Options{ConflictPolicy::Ask,true,chosen});
  auto planned=resolved.plan(cl,cr);
  assert(std::none_of(planned.begin(),planned.end(),[](const Decision& d){return d.action==Action::Conflict;}));
  auto applied=resolved.run(cl,cr);
  assert(applied.conflicts.empty()&&applied.copied==2&&applied.removed==1);
  assert(read(cr/"left.txt")=="LEFT");
  assert(read(cl/"right.txt")=="RIGHT");
  assert(read(cr/"skip.txt")=="RIGHT"&&read(cl/"skip.txt")=="LEFT");
  assert(!fs::exists(cr/"deleted.txt"));
  // Skip preserves both copies. On the next scan, different mtimes select
  // the newer copy; equal mtimes produce an explicit conflict. Windows has
  // coarser timestamp resolution, so neither outcome can be assumed here.
  const auto remaining=first.plan(cl,cr);
  const auto skipped=std::find_if(remaining.begin(),remaining.end(),[](const Decision& d){
   return d.relative=="skip.txt";
  });
  if(skipped==remaining.end() ||
     (skipped->action!=Action::Conflict &&
      skipped->action!=Action::CopyLeftToRight &&
      skipped->action!=Action::CopyRightToLeft))
   throw std::runtime_error("Skipped divergence must remain detectable");
  if(read(cl/"skip.txt")!="LEFT" || read(cr/"skip.txt")!="RIGHT")
   throw std::runtime_error("Planning must not overwrite skipped copies");

  // Progress and cancellation: stop after the first complete file,
  // leaving state unchanged so the next run can reconcile safely.
  auto cancelA=root/"cancelA",cancelB=root/"cancelB";
  fs::create_directories(cancelA);fs::create_directories(cancelB);
  write(cancelA/"a.txt","one");write(cancelA/"b.txt","two");
  auto cancelState=root/"state"/"cancel.state";
  EngineWithoutNativeRecycle cancellable(cancelState);
  bool stop=false;
  std::vector<Progress> reported;
  RunCallbacks callbacks;
  callbacks.cancelled=[&]{return stop;};
  callbacks.progress=[&](const Progress& p){
   reported.push_back(p);
   if(p.phase==Phase::Copy&&p.completed==1)stop=true;
  };
  bool cancelled=false;
  try{cancellable.run(cancelA,cancelB,false,callbacks);}
  catch(const RunCancelled&){cancelled=true;}
  assert(cancelled&&!fs::exists(cancelState));
  assert(cancellable.has_pending_recovery());
  auto cancelLog=root/"state"/"logs"/"cancel.state.jsonl";
  assert(fs::exists(cancelLog));
  assert(read(cancelLog).find("\"event\":\"cancelled\"")!=std::string::npos);
  assert(reported.size()>2);
  assert(fs::exists(cancelB/"a.txt")&&!fs::exists(cancelB/"b.txt"));
  assert(throws([&]{cancellable.run(cancelA,cancelB);}));
  auto preview=cancellable.plan(cancelA,cancelB);
  assert(!preview.empty());
  EngineWithoutNativeRecycle recovery(cancelState,Options{ConflictPolicy::Ask,true,{},true});
  assert(recovery.run(cancelA,cancelB).conflicts.empty());
  assert(!recovery.has_pending_recovery());
  assert(read(cancelB/"b.txt")=="two");
  assert(recovery.run(cancelA,cancelB).copied==0);

  // Cancellation during a scan never copies anything and never updates state.
  auto scanL=root/"scan-cancel-l",scanR=root/"scan-cancel-r";
  fs::create_directories(scanL);fs::create_directories(scanR);
  write(scanL/"a","first");write(scanL/"b","second");
  EngineWithoutNativeRecycle scanEngine(root/"state"/"scan-cancel.state");
  bool stopScan=false;
  RunCallbacks scanCallbacks{
   [&]{return stopScan;},
   [&](const Progress& p){if(p.phase==Phase::ScanLeft&&p.completed==1)stopScan=true;}
  };
  assert(throws([&]{scanEngine.run(scanL,scanR,false,scanCallbacks);}));
  assert(!fs::exists(scanR/"a"));
  assert(!fs::exists(root/"state"/"scan-cancel.state"));
  assert(!scanEngine.has_pending_recovery());
  // The recovery journal is specific to its ordered pair of endpoints.
  auto pairState=root/"state"/"mismatch.state";
  EngineWithoutNativeRecycle wrongPair(pairState);
  write(scanL/"will-cancel","test");
  bool stopAfter=false;
  RunCallbacks afterFirst{{[&]{return stopAfter;}},[&](const Progress& p){
   if(p.phase==Phase::Copy&&p.completed==1)stopAfter=true;
  }};
  assert(throws([&]{wrongPair.run(scanL,scanR,false,afterFirst);}));
  assert(wrongPair.has_pending_recovery());
  assert(throws([&]{EngineWithoutNativeRecycle(pairState,Options{ConflictPolicy::Ask,true,{},true}).run(a,b);}));
  assert(wrongPair.has_pending_recovery());
  EngineWithoutNativeRecycle correctPair(pairState,Options{ConflictPolicy::Ask,true,{},true});
  assert(correctPair.run(scanL,scanR).conflicts.empty());
  assert(!correctPair.has_pending_recovery());
  // The journal must fail closed if the metadata is tampered with.
  auto corruptState=root/"state"/"corrupt-journal.state";
  write(root/"state"/"corrupt-journal.state.journal","INVALID HEADER\n");
  assert(throws([&]{EngineWithoutNativeRecycle(corruptState,Options{ConflictPolicy::Ask,true,{},true}).run(scanL,scanR);}));
  // An orphaned local temporary upload must fail closed, not sync junk.
  write(scanL/"user.bin.synchrorep-write-12345.tmp","partial");
  assert(throws([&]{correctPair.plan(scanL,scanR);}));
  fs::remove(scanL/"user.bin.synchrorep-write-12345.tmp");
  // An explicitly skipped conflict is not learned into history.
  auto explicitSkip=resolved.plan(cl,cr);
  assert(std::none_of(explicitSkip.begin(),explicitSkip.end(),[](const Decision& d){
   return d.action==Action::Conflict;
  }));
  assert(std::any_of(explicitSkip.begin(),explicitSkip.end(),[](const Decision& d){
   return d.action==Action::Skip;
  }));
  // Recycle policy uses the recycler only when supported and enabled.
  auto recycleL=root/"recycle-left",recycleR=root/"recycle-right";
  fs::create_directories(recycleL);fs::create_directories(recycleR);
  LocalEndpoint recycleSource(recycleL);
  TestRecyclingEndpoint recycleDest(recycleR,root/"fake-recycle-bin");
  Engine recycleEngine(root/"state"/"recycle.state");
  write(recycleL/"gone.txt","valuable");
  assert(recycleEngine.run(recycleSource,recycleDest).copied==1);
  fs::remove(recycleL/"gone.txt");
  auto recycled=recycleEngine.run(recycleSource,recycleDest);
  assert(recycled.removed==1&&recycled.recycled==1&&recycled.archived==1);
  assert(recycleDest.recycled==1&&recycleDest.permanently_deleted==0);
  assert(read(root/"fake-recycle-bin"/"gone.txt")=="valuable");
  // Explicit user opt-out invokes a normal removal instead.
  auto permanentL=root/"permanent-left",permanentR=root/"permanent-right";
  fs::create_directories(permanentL);fs::create_directories(permanentR);
  LocalEndpoint permanentSource(permanentL);
  TestRecyclingEndpoint permanentDest(permanentR,root/"unused-recycle-bin");
  Engine permanent(root/"state"/"permanent.state",
   Options{ConflictPolicy::Ask,true,{},false,true,false});
  write(permanentL/"gone.txt","old");
  permanent.run(permanentSource,permanentDest);
  fs::remove(permanentL/"gone.txt");
  auto permanentResult=permanent.run(permanentSource,permanentDest);
  assert(permanentResult.removed==1&&permanentResult.recycled==0);
  assert(permanentDest.permanently_deleted==1&&permanentDest.recycled==0);
  // Recycle errors must never be downgraded to permanent deletion.
  auto failureL=root/"failure-left",failureR=root/"failure-right";
  fs::create_directories(failureL);fs::create_directories(failureR);
  LocalEndpoint failureSource(failureL);
  TestRecyclingEndpoint failureDest(failureR,root/"failed-bin");
  Engine failureEngine(root/"state"/"failed-recycle.state");
  write(failureL/"keep.txt","keep");
  failureEngine.run(failureSource,failureDest);
  fs::remove(failureL/"keep.txt");
  failureDest.fail=true;
  assert(throws([&]{failureEngine.run(failureSource,failureDest);}));
  assert(failureDest.permanently_deleted==0);
  assert(fs::exists(failureR/"keep.txt"));
  assert(failureEngine.has_pending_recovery());
#ifndef _WIN32
  assert(!supports_native_recycle(recycleR));
#endif
  // A malformed history or a symlink in the tree must fail closed.
  write(root/"state"/"bad.state","INVALID-HEADER\n");
  assert(throws([&]{EngineWithoutNativeRecycle(root/"state"/"bad.state").run(u,v);}));
#ifndef _WIN32
  fs::create_symlink(u/"dry.txt",v/"link.txt");
  assert(throws([&]{dry.run(u,v);}));
  fs::remove(v/"link.txt");
#endif
  // Transfer regressions use isolated roots to avoid affecting historical scenarios.
  auto transferA=root/"transfer-left",transferB=root/"transfer-right";
  fs::create_directories(transferA);
  fs::create_directories(transferB);
  EngineWithoutNativeRecycle transferEngine(root/"state"/"transfer.state");
  // Empty files must copy successfully; streambuf insertion sets failbit for zero bytes.
  write(transferA/"empty.odg","");
  assert(transferEngine.run(transferA,transferB).copied==1);
  assert(fs::exists(transferB/"empty.odg")&&fs::file_size(transferB/"empty.odg")==0);
  // Exercise transfers crossing the streaming buffer boundary.
  write(transferA/"large.odg",std::string(150000,'x'));
  assert(transferEngine.run(transferA,transferB).copied==1);
  assert(read(transferB/"large.odg")==std::string(150000,'x'));
  // A truncated source must not replace the existing target.
  {
   LocalEndpoint destination(transferB);
   std::istringstream truncated("short");
   Entry expected{};
   expected.size=20;
   write(transferB/"preserved.odg","original");
   assert(throws([&]{destination.write("preserved.odg",truncated,expected);}));
   assert(read(transferB/"preserved.odg")=="original");
  }
  fs::remove_all(root);
  std::cout<<"Engine regression suite: OK\n";
 }catch(...){fs::remove_all(root);throw;}
}
