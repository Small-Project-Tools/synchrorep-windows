#include "synchrorep/engine.hpp"
#include <iostream>
int main(int argc,char**argv){
 if(argc<4||argc>5){
  std::cerr<<"Usage: synchrorep LEFT RIGHT STATE_FILE [--dry-run]\n";
  std::cerr<<"Endpoint types: local filesystem, wsl://distro/path (Windows only), FTP/SFTP when built with SYNCHROREP_REMOTE=ON.\n";
  return 2;
 }
 try{
  const bool dry=argc==5&&std::string(argv[4])=="--dry-run";
  if(argc==5&&!dry)return 2;
  auto left=synchrorep::open_endpoint(argv[1]);
  auto right=synchrorep::open_endpoint(argv[2]);
  synchrorep::Engine engine(argv[3]);
  const auto decisions=engine.plan(*left,*right);
  for(const auto& d:decisions){
   if(d.action==synchrorep::Action::None)continue;
   std::cout<<static_cast<int>(d.action)<<" "<<d.relative<<"\n";
  }
  if(dry)return 0;
  auto result=engine.run(*left,*right);
  for(auto& path:result.conflicts)std::cerr<<"CONFLICT: "<<path<<"\n";
  std::cout<<"copied="<<result.copied<<" removed="<<result.removed
           <<" conflicts="<<result.conflicts.size()<<"\n";
  return result.conflicts.empty()?0:3;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
