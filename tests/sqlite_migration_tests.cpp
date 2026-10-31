#include "synchrorep/engine.hpp"
#include "synchrorep/sqlite_history.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
using namespace synchrorep;
static void put(const fs::path& path,const char* contents){
 fs::create_directories(path.parent_path());
 std::ofstream file(path,std::ios::binary);file<<contents;
}
int main(){
 auto root=fs::temp_directory_path()/("synchrorep-migration-"+std::to_string(
  std::chrono::steady_clock::now().time_since_epoch().count()));
 auto a=root/"left",b=root/"right",state=root/"state"/"existing.state";
 fs::create_directories(a);fs::create_directories(b);fs::create_directories(state.parent_path());
 try{
  put(a/"existing.txt","previous");put(b/"existing.txt","previous");
  LocalEndpoint left(a),right(b);
  auto la=left.scan().at("existing.txt"),ra=right.scan().at("existing.txt");
  // A real SYNCHROREP4 historical record, with pair identity and one file.
  {
   std::ofstream out(state,std::ios::binary);
   out<<"SYNCHROREP4\n"<<std::quoted(left.identity())<<' '<<std::quoted(right.identity())<<'\n'
      <<std::quoted(std::string("existing.txt"))<<' '
      <<la.exists<<' '<<la.directory<<' '<<la.size<<' '<<la.modified<<' '<<std::quoted(la.hash)<<' '
      <<ra.exists<<' '<<ra.directory<<' '<<ra.size<<' '<<ra.modified<<' '<<std::quoted(ra.hash)<<'\n';
  }
  const auto textSize=fs::file_size(state);
  Engine engine(state);
  auto before=engine.plan(left,right);
  assert(before.size()==1&&before[0].action==Action::None);
  auto outcome=engine.run(left,right);
  assert(outcome.copied==0&&outcome.removed==0);
  assert(fs::file_size(state)==textSize);
  SqliteHistory sqlite(fs::path(state.string()+".sqlite"));
  assert(sqlite.has_pair(left.identity(),right.identity()));
  assert(sqlite.load(left.identity(),right.identity()).size()==1);
  // Once activated, the database must be reused without rewriting text history.
  assert(engine.run(left,right).copied==0);
  assert(fs::file_size(state)==textSize);
  fs::remove_all(root);
  std::cout<<"Legacy SYNCHROREP4 migrated and retained unchanged\n";
 }catch(...){fs::remove_all(root);throw;}
}
