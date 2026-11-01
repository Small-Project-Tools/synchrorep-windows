// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/engine.hpp"
#include "synchrorep/mirror_monitor.hpp"
#include <csignal>
#include <iostream>
namespace {
volatile std::sig_atomic_t stopped=0;
void stop_signal(int){stopped=1;}
}
int main(int argc,char**argv){
 if(argc<4||argc>9){
  std::cerr<<"Usage: synchrorep LEFT RIGHT STATE_FILE [--dry-run] [--resume] [--no-recycle] [--mirror] [--watch]\n";
  std::cerr<<"Endpoint types: local filesystem, wsl://distro/path (Windows only), FTP/SFTP when built with SYNCHROREP_REMOTE=ON.\n";
  return 2;
 }
 try{
  bool dry=false,resume=false,recycle=true,mirror=false,watch=false;
  for(int i=4;i<argc;++i){
   std::string option=argv[i];
   if(option=="--dry-run")dry=true;
   else if(option=="--resume")resume=true;
   else if(option=="--no-recycle")recycle=false;
   else if(option=="--mirror")mirror=true;
   else if(option=="--watch")watch=true;
   else return 2;
  }
  if(watch&&!mirror)throw std::invalid_argument("--watch requires --mirror");
  if(watch&&dry)throw std::invalid_argument("--watch cannot be combined with --dry-run");
  if(mirror&&synchrorep::endpoint_kind(argv[1])!=synchrorep::EndpointKind::Local)
   throw std::invalid_argument("--mirror source must be local");
  auto left=synchrorep::open_endpoint(argv[1]);
  auto right=synchrorep::open_endpoint(argv[2]);
  synchrorep::Options options;
  options.resume_interrupted=resume;
  options.recycle_local_deletions=recycle;
  if(mirror)options.mode=synchrorep::SyncMode::Mirror;
  synchrorep::Engine engine(argv[3],options);
  const auto decisions=engine.plan(*left,*right);
  for(const auto& d:decisions){
   if(d.action==synchrorep::Action::None)continue;
   std::cout<<static_cast<int>(d.action)<<" "<<d.relative<<"\n";
  }
  if(dry)return 0;
  synchrorep::Result result;
  if(watch){
   std::signal(SIGINT,stop_signal);
   std::signal(SIGTERM,stop_signal);
   synchrorep::RunCallbacks callbacks;
   callbacks.cancelled=[] {return stopped!=0;};
   synchrorep::MirrorMonitor monitor;
   result=monitor.run(*left,*right,engine,
      static_cast<synchrorep::LocalEndpoint*>(left.get())->root(),callbacks);
  }else result=engine.run(*left,*right);
  for(auto& path:result.conflicts)std::cerr<<"CONFLICT: "<<path<<"\n";
  std::cout<<"copied="<<result.copied<<" removed="<<result.removed
           <<" conflicts="<<result.conflicts.size()<<"\n";
  return result.conflicts.empty()?0:3;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
