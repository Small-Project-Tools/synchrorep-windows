#include "synchrorep/engine.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace synchrorep;
static void write(const fs::path& p,const std::string& text){fs::create_directories(p.parent_path());std::ofstream(p)<<text;}
static std::string read(const fs::path&p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(){
 auto root=fs::temp_directory_path()/("synchrorep-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 auto a=root/"a",b=root/"b";fs::create_directories(a);fs::create_directories(b);
 try{
  Engine e(root/"state"/"history.txt");
  write(a/"file.txt","a");assert(e.run(a,b).copied==1);assert(read(b/"file.txt")=="a");
  assert(e.run(a,b).copied==0);
  write(b/"only-right.txt","right");assert(e.run(a,b).copied==1);assert(read(a/"only-right.txt")=="right");
  fs::remove(a/"file.txt");assert(e.run(a,b).removed==1);assert(!fs::exists(b/"file.txt"));
  write(a/"conflict.txt","0");assert(e.run(a,b).copied==1);
  write(a/"conflict.txt","LEFT");write(b/"conflict.txt","RIGHT");
  assert(e.run(a,b).conflicts.size()==1);
  assert(read(a/"conflict.txt")=="LEFT"&&read(b/"conflict.txt")=="RIGHT");
  fs::remove_all(root);std::cout<<"All tests passed\n";
 }catch(...){fs::remove_all(root);throw;}
}
