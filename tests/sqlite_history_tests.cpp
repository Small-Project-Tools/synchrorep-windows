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
  StoredHistory entries;
  entries["fichier special ' x.txt"]={{true,false,25,123,""},{true,false,25,124,""}};
  storage.save("left","right",entries);
  auto recovered=storage.load("left","right");
  assert(recovered.size()==1);
  assert(recovered.at("fichier special ' x.txt").right.modified==124);
  assert(storage.load("other","right").empty());
  storage.save("left","right",{});
  assert(storage.load("left","right").empty());
  assert(storage.load("other","right").empty());
  fs::remove_all(root);
  std::cout<<"SQLite indexed history, isolation, persistence and replacement OK\n";
 }catch(...){fs::remove_all(root);throw;}
}
