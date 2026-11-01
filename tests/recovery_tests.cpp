// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/recovery_store.hpp"
#include "synchrorep/sha256.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace synchrorep;

struct FixedEndpoint final : Endpoint {
 std::string bytes;
 std::string identity() const override {return "test://fixed";}
 Snapshot scan() const override {return {};}
 std::unique_ptr<std::istream> read(const std::string&) const override {
  return std::make_unique<std::istringstream>(bytes);
 }
 void write(const std::string&,std::istream&,const Entry&) override {throw std::runtime_error("No writes");}
 void make_directory(const std::string&) override {throw std::runtime_error("No mkdir");}
 void remove(const std::string&) override {throw std::runtime_error("No deletion");}
};
static std::string digest(const std::string& text){
 std::istringstream input(text);return sha256(input,text.size());
}
static std::string load(const fs::path& path){
 std::ifstream file(path);return {std::istreambuf_iterator<char>(file),{}};
}
int main(){
 auto root=fs::temp_directory_path()/("synchrorep-recovery-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  FixedEndpoint endpoint;
  endpoint.bytes="wrong content";
  RecoveryStore store(root/"archive-1");
  auto expected=Entry{true,false,11,0,digest("right bytes")};
  bool rejected=false;
  try{store.preserve(endpoint,"path/file.txt",expected,"before-delete");}
  catch(const std::runtime_error&){rejected=true;}
  assert(rejected);
  assert(!fs::exists(store.directory()/"data"/"path"/"file.txt"));
  assert(load(store.directory()/"manifest.tsv")=="SYNCHROREP-RECOVERY1\n");
  endpoint.bytes="right bytes";
  store.preserve(endpoint,"path/file.txt",expected,"before-delete");
  assert(load(store.directory()/"data"/"path"/"file.txt")=="right bytes");
  assert(load(store.directory()/"manifest.tsv").find("before-delete")!=std::string::npos);
  bool duplicate=false;
  try{store.preserve(endpoint,"path/file.txt",expected,"before-delete");}
  catch(const std::runtime_error&){duplicate=true;}
  assert(duplicate);
  bool traversal=false;
  try{store.preserve(endpoint,"../escape",expected,"before-delete");}
  catch(const std::invalid_argument&){traversal=true;}
  assert(traversal);
  bool illegal=false;
  try{store.preserve(endpoint,"file:alternate",expected,"before-delete");}
  catch(const std::invalid_argument&){illegal=true;}
  assert(illegal);
  fs::remove_all(root);
 }catch(...){fs::remove_all(root);throw;}
}
