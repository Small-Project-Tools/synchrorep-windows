#include "synchrorep/engine.hpp"
#include "synchrorep/remote_endpoint.hpp"
#include <cassert>
#include <algorithm>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace synchrorep;
static std::string env(const char* key){const char* p=std::getenv(key);return p?p:"";}
static void write(const fs::path& p,const std::string& text){fs::create_directories(p.parent_path());std::ofstream(p)<<text;}
static std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(){
 auto url=env("SYNCHROREP_TEST_REMOTE_URL");
 if(url.empty()){std::cout<<"SKIP remote integration: SYNCHROREP_TEST_REMOTE_URL unset\n";return 0;}
 RemoteCredentials credentials{env("SYNCHROREP_REMOTE_USER"),env("SYNCHROREP_REMOTE_PASSWORD"),
  env("SYNCHROREP_SFTP_KNOWN_HOSTS"),env("SYNCHROREP_SFTP_PRIVATE_KEY")};
 auto path=fs::temp_directory_path()/"synchrorep-test-remote";
 fs::remove_all(path);fs::create_directories(path/"local");
 try{
  std::cerr<<"Remote integration endpoint="<<url<<"\n";
  RemoteEndpoint remote(url,credentials);
  LocalEndpoint local(path/"local");
  Engine engine(path/"history.txt");
  std::vector<Progress> states;
  RunCallbacks track{[]{return false;},[&](const Progress& p){states.push_back(p);}};
  write(path/"local"/"alpha.txt","hello");
  auto first=engine.run(local,remote,false,track);assert(first.conflicts.empty()&&first.copied==1);
  assert(std::any_of(states.begin(),states.end(),[](const Progress& p){return p.phase==Phase::Copy&&p.completed==1;}));
  assert(std::any_of(states.begin(),states.end(),[](const Progress& p){return p.phase==Phase::Complete;}));
  auto snapshot=remote.scan();assert(snapshot.count("alpha.txt")==1);
  assert(snapshot.at("alpha.txt").size==5);
  assert(engine.run(local,remote).copied==0);
  // A same-size modification with unchanged mtime is still propagated.
  auto remote_t=fs::last_write_time(path/"local"/"alpha.txt");
  write(path/"local"/"alpha.txt","world");
  fs::last_write_time(path/"local"/"alpha.txt",remote_t);
  auto remoteOverwrite=engine.run(local,remote);
  assert(remoteOverwrite.copied==1 && remoteOverwrite.archived==1);
  // Before remote alpha.txt was overwritten, the old contents were downloaded
  // into local recovery storage, with their expected SHA-256 verified.
  auto archiveRoot=path/"recovery"/"history.txt";
  bool archivedHello=false;
  for(const auto& item:fs::recursive_directory_iterator(archiveRoot))
   if(item.is_regular_file() && item.path().filename()=="alpha.txt"){
    assert(read(item.path())=="hello");
    archivedHello=true;
   }
  assert(archivedHello);
  assert(engine.run(local,remote).copied==0);
  std::istringstream data("REMOTE-DATA");
  Entry e{true,false,11,0,{}};
  remote.write("remote.txt",data,e);
  assert(engine.run(local,remote).copied==1);
  assert(read(path/"local"/"remote.txt")=="REMOTE-DATA");
  write(path/"local"/"nested"/"space name.txt","NEST");
  assert(engine.run(local,remote).conflicts.empty());
  assert(remote.scan().count("nested/space name.txt")==1);
  // A failed short upload must never replace an existing file.
  std::istringstream incomplete("BAD");
  bool short_rejected=false;
  try {remote.write("remote.txt",incomplete,Entry{true,false,11,0,{}});}
  catch(const std::exception&) {short_rejected=true;}
  assert(short_rejected);
  assert(remote.scan().at("remote.txt").size==11);
  // Abort an active curl transfer, not just at the boundary between files.
  int checks=0;
  remote.set_cancellation([&]{return ++checks>=2;});
  bool transfer_cancelled=false;
  try{
   auto ignored=remote.read("remote.txt");
   (void)ignored;
  }catch(const RunCancelled&){transfer_cancelled=true;}
  remote.set_cancellation({});
  assert(transfer_cancelled);
  checks=0;
  remote.set_cancellation([&]{return ++checks>=2;});
  std::istringstream upload("REPLACEMENT");
  bool upload_cancelled=false;
  try{remote.write("remote.txt",upload,Entry{true,false,11,0,{}});}
  catch(const RunCancelled&){upload_cancelled=true;}
  remote.set_cancellation({});
  assert(upload_cancelled);
  const auto after_cancel=remote.scan();
  assert(after_cancel.at("remote.txt").size==11);
  for(const auto& [name,_]:after_cancel)
   assert(name.find(".synchrorep-upload-")==std::string::npos);
  // A change created remotely must propagate to the local side.
  std::istringstream updated("REMOTE-UPDATED");
  remote.write("remote.txt",updated,Entry{true,false,14,0,{}});
  assert(engine.run(local,remote).copied==1);
  assert(read(path/"local"/"remote.txt")=="REMOTE-UPDATED");
  fs::remove(path/"local"/"alpha.txt");
  auto remoteRemoval=engine.run(local,remote);
  assert(remoteRemoval.removed==1 && remoteRemoval.archived==1);
  assert(remote.scan().count("alpha.txt")==0);
  bool archivedWorld=false;
  for(const auto& item:fs::recursive_directory_iterator(archiveRoot))
   if(item.is_regular_file() && item.path().filename()=="alpha.txt" && read(item.path())=="world")
    archivedWorld=true;
  assert(archivedWorld);
  // Mirror mode: remote target is authoritative only as a destination.
  auto mirrorLocal=path/"mirror-source";
  fs::create_directories(mirrorLocal);
  write(mirrorLocal/"mirror.txt","LOCAL-MIRROR");
  LocalEndpoint mirrorSource(mirrorLocal);
  Options mirrorOptions;
  mirrorOptions.mode=SyncMode::Mirror;
  Engine mirrorEngine(path/"mirror-history",mirrorOptions);
  auto mirrorFirst=mirrorEngine.run(mirrorSource,remote);
  assert(mirrorFirst.copied==1 && mirrorFirst.removed>=1);
  assert(remote.scan().count("mirror.txt")==1);
  assert(!fs::exists(mirrorLocal/"remote.txt"));
  assert(!fs::exists(mirrorLocal/"nested"));
  std::istringstream external("EXTERNAL");
  remote.write("mirror.txt",external,Entry{true,false,8,0,{}});
  assert(mirrorEngine.run(mirrorSource,remote).copied==1);
  assert(read(mirrorLocal/"mirror.txt")=="LOCAL-MIRROR");
  write(mirrorLocal/"mirror.txt","LOCAL-UPDATED");
  assert(mirrorEngine.run(mirrorSource,remote).copied==1);
  auto listed=remote.scan();
  assert(listed.at("mirror.txt").size==13);
  // Remote-only files are purged, never copied back.
  std::istringstream stray("STRAY");
  remote.write("stray.txt",stray,Entry{true,false,5,0,{}});
  auto mirrored=mirrorEngine.run(mirrorSource,remote);
  assert(mirrored.removed==1);
  assert(!fs::exists(mirrorLocal/"stray.txt"));
  assert(remote.scan().count("stray.txt")==0);
  fs::remove(mirrorLocal/"mirror.txt");
  assert(mirrorEngine.run(mirrorSource,remote).removed==1);
  assert(remote.scan().empty());
  fs::remove_all(path);
  std::cout<<"Remote integration tests passed for "<<remote.identity()<<"\n";
 }catch(const std::exception& error){
  std::cerr<<"Remote integration test failed ("<<url<<"): "<<error.what()<<"\n";
  fs::remove_all(path);return 1;
 }
}
