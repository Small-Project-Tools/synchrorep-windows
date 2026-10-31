#include "synchrorep/engine.hpp"
#include "synchrorep/remote_endpoint.hpp"
#include <cassert>
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
  RemoteEndpoint remote(url,credentials);
  LocalEndpoint local(path/"local");
  Engine engine(path/"history.txt");
  write(path/"local"/"alpha.txt","hello");
  auto first=engine.run(local,remote);assert(first.conflicts.empty()&&first.copied==1);
  auto snapshot=remote.scan();assert(snapshot.count("alpha.txt")==1);
  assert(snapshot.at("alpha.txt").size==5);
  assert(engine.run(local,remote).copied==0);
  // A same-size modification with unchanged mtime is still propagated.
  auto remote_t=fs::last_write_time(path/"local"/"alpha.txt");
  write(path/"local"/"alpha.txt","world");
  fs::last_write_time(path/"local"/"alpha.txt",remote_t);
  assert(engine.run(local,remote).copied==1);
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
  // A change created remotely must propagate to the local side.
  std::istringstream updated("REMOTE-UPDATED");
  remote.write("remote.txt",updated,Entry{true,false,14,0,{}});
  assert(engine.run(local,remote).copied==1);
  assert(read(path/"local"/"remote.txt")=="REMOTE-UPDATED");
  fs::remove(path/"local"/"alpha.txt");
  assert(engine.run(local,remote).removed==1);
  assert(remote.scan().count("alpha.txt")==0);
  fs::remove_all(path);
  std::cout<<"Remote integration tests passed for "<<remote.identity()<<"\n";
 }catch(...){fs::remove_all(path);throw;}
}
