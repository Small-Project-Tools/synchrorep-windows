#include "synchrorep/engine.hpp"
#include <iostream>
int main(int argc,char**argv){
 if(argc!=4){std::cerr<<"Usage: synchrorep LEFT RIGHT STATE_FILE\n";return 2;}
 try{
  synchrorep::Engine e(argv[3]);auto result=e.run(argv[1],argv[2]);
  for(auto& x:result.conflicts)std::cerr<<"CONFLICT: "<<x<<"\n";
  std::cout<<"copied="<<result.copied<<" removed="<<result.removed<<" conflicts="<<result.conflicts.size()<<"\n";
  return result.conflicts.empty()?0:3;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
