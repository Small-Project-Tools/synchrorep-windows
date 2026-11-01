// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/engine.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace synchrorep;

class CountingEndpoint final : public Endpoint {
 LocalEndpoint local_;
public:
 mutable unsigned reads=0;
 explicit CountingEndpoint(const fs::path& path):local_(path){}
 std::string identity()const override{return local_.identity();}
 Snapshot scan()const override{return local_.scan();}
 std::unique_ptr<std::istream> read(const std::string& path)const override{
  ++reads;
  return local_.read(path);
 }
 void write(const std::string& path,std::istream& data,const Entry& source)override{
  local_.write(path,data,source);
 }
 void make_directory(const std::string& path)override{local_.make_directory(path);}
 void remove(const std::string& path)override{local_.remove(path);}
};
int main(){
 auto base=fs::temp_directory_path()/("synchrorep-hash-policy-"+
   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 auto a=base/"a",b=base/"b";
 fs::create_directories(a);fs::create_directories(b);
 try{
  {std::ofstream out(a/"single");out<<"single";}
  CountingEndpoint left(a),right(b);
  Engine first(base/"first.state");
  assert(first.plan(left,right).size()==1);
  assert(left.reads==0&&right.reads==0); // Hash only common files.
  {std::ofstream out(a/"common");out<<"AAAA";}
  {std::ofstream out(b/"common");out<<"AAAA";}
  auto t=fs::last_write_time(a/"common");
  fs::last_write_time(b/"common",t);
  auto initial=first.plan(left,right);
  assert(initial.size()==2);
  assert(left.reads==1&&right.reads==1); // Mandatory first comparison.
  assert(first.run(left,right).copied==1);
  left.reads=right.reads=0;
  assert(first.plan(left,right).size()==2);
  assert(left.reads==0&&right.reads==0); // Fast mode uses metadata only.

  Options withHashes;withHashes.verify_hash=true;
  Engine checked(base/"first.state",withHashes);
  {std::ofstream out(a/"common",std::ios::trunc);out<<"BBBB";}
  fs::last_write_time(a/"common",t);
  left.reads=right.reads=0;
  auto decisions=checked.plan(left,right);
  assert(left.reads==2&&right.reads==2); // Both common files, including single.
  bool conflict=false;
  for(const auto& d:decisions)if(d.relative=="common")conflict=d.action==Action::Conflict;
  assert(conflict); // Equal dates: never guess which version is newer.
  fs::last_write_time(a/"common",t+std::chrono::seconds(4));
  decisions=checked.plan(left,right);
  bool newer=false;
  for(const auto& d:decisions)if(d.relative=="common")newer=d.action==Action::CopyLeftToRight;
  assert(newer); // Latest timestamp wins.
  fs::remove_all(base);
  std::cout<<"Optional digest, first comparison and timestamp policy passed\n";
 }catch(...){fs::remove_all(base);throw;}
}
