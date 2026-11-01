// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/sqlite_history.hpp"
#include <stdexcept>
#define assert(expression) do { \
 if (!(expression)) throw std::runtime_error("Assertion failed at line " + \
 std::to_string(__LINE__) + ": " #expression); \
} while (false)
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
  entries["project..backup/file..txt"]={{true,false,2,23,""},{true,false,2,24,""}};
  entries["fichier special ' x.txt"]={{true,false,25,123,""},{true,false,25,124,""}};
  storage.save("left","right",entries);
  assert(storage.has_any_pair()&&storage.has_pair("left","right"));
  auto recovered=storage.load("left","right");
  assert(recovered.size()==2);
  assert(recovered.at("project..backup/file..txt").right.modified==24);
  assert(recovered.at("fichier special ' x.txt").right.modified==124);
  assert(storage.load("other","right").empty());
  // A failed replacement must roll back the entire previous snapshot.
  StoredHistory invalid=entries;
  invalid["../escape"]={{true,false,1,1,""},{true,false,1,1,""}};
  bool rejected=false;
  try{storage.save("left","right",invalid);}catch(const std::exception&){rejected=true;}
  assert(rejected);
  assert(storage.load("left","right").size()==2);
  storage.save("left","right",{});
  assert(storage.load("left","right").empty());
  assert(storage.has_pair("left","right"));
  assert(storage.load("other","right").empty());
  storage.save("left","right",entries);
  storage.save("other","pair",entries);
  storage.erase_pair("left","right");
  assert(!storage.has_pair("left","right"));
  assert(storage.load("left","right").empty());
  assert(storage.has_pair("other","pair"));
  assert(storage.load("other","pair").size()==2);
  storage.erase_pair("left","right"); // idempotent
  assert(storage.load("other","pair").size()==2);
  // Deletion must survive reopening the database, without affecting another pair.
  SqliteHistory reopened(root/"history.sqlite");
  assert(!reopened.has_pair("left","right"));
  assert(reopened.load("left","right").empty());
  assert(reopened.has_pair("other","pair"));
  assert(reopened.load("other","pair").size()==2);
  fs::remove_all(root);
  std::cout<<"SQLite indexed history, isolation, persistence and replacement OK\n";
 }catch(...){fs::remove_all(root);throw;}
}
