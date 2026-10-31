#include "synchrorep/sqlite_history.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace synchrorep;
int main(){
 auto root=fs::temp_directory_path()/("sqlite-history-"+std::to_string(
  std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  SqliteHistory storage(root/"history.sqlite");
  assert(storage.load("left","right").empty());
  assert(!storage.has_any_pair());
  StoredHistory entries;
  entries["fichier special ' x.txt"]={{true,false,25,123,""},{true,false,25,124,""}};
  storage.save("left","right",entries);
  assert(storage.has_any_pair()&&storage.has_pair("left","right"));
  auto recovered=storage.load("left","right");
  assert(recovered.size()==1);
  assert(recovered.at("fichier special ' x.txt").right.modified==124);
  assert(storage.load("other","right").empty());
  storage.save("left","right",{});
  assert(storage.load("left","right").empty());
  assert(storage.has_pair("left","right"));
  assert(storage.load("other","right").empty());
  fs::remove_all(root);
  std::cout<<"SQLite indexed history, isolation, persistence and replacement OK\n";
 }catch(...){fs::remove_all(root);throw;}
}
