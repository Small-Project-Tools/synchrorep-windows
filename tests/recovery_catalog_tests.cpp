#include "synchrorep/recovery_catalog.hpp"
#include "synchrorep/recovery_store.hpp"
#include "synchrorep/sha256.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
using namespace synchrorep;
struct FixedEndpoint final:Endpoint {
 std::string text;
 std::string identity()const override{return "file:test";}
 Snapshot scan()const override{return {};}
 std::unique_ptr<std::istream> read(const std::string&)const override{return std::make_unique<std::istringstream>(text);}
 void write(const std::string&,std::istream&,const Entry&)override{}
 void make_directory(const std::string&)override{}
 void remove(const std::string&)override{}
};
std::string digest(const std::string& text){std::istringstream in(text);return sha256(in,text.size());}
void write(const fs::path& p,const std::string& s){fs::create_directories(p.parent_path());std::ofstream(p,std::ios::binary)<<s;}
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
bool fails(const auto& callback){try{callback();return false;}catch(const std::exception&){return true;}}
int main(){
 const auto root=fs::temp_directory_path()/("synchrorep-recovery-catalog-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  auto backup=root/"recovery"/"state"/"run01"/"left";
  FixedEndpoint local;local.text="PREVIOUS FILE";
  RecoveryStore archive(backup);
  Entry entry{true,false,13,0,digest(local.text)};
  archive.preserve(local,"nested/file.txt",entry,"overwrite-left");
  auto records=RecoveryCatalog::list(root/"recovery");
  assert(records.size()==1);
  assert(records[0].relative=="nested/file.txt");
  auto exportRoot=root/"export";fs::create_directories(exportRoot);
  auto destination=RecoveryCatalog::export_item(records[0],exportRoot);
  assert(read(destination)=="PREVIOUS FILE");
  assert(fails([&]{RecoveryCatalog::export_item(records[0],exportRoot);}));
  assert(read(backup/"data"/"nested"/"file.txt")=="PREVIOUS FILE");
  assert(fails([&]{RecoveryCatalog::export_item(records[0],backup/"data");}));
  write(backup/"data"/"nested"/"file.txt","CORRUPT-DATA");
  auto alternate=root/"alternate";fs::create_directories(alternate);
  assert(fails([&]{RecoveryCatalog::export_item(records[0],alternate);}));
  assert(!fs::exists(alternate/"nested"/"file.txt"));
  write(backup/"data"/"nested"/"file.txt","PREVIOUS FILE");
  auto manifest=backup/"manifest.tsv";
  auto trusted=read(manifest);
  write(manifest,trusted+"\"delete\" \"file:test\" \"../escape\" 0 4 \""+digest("oops")+"\" \"data/../escape\"\n");
  assert(fails([&]{RecoveryCatalog::list(root/"recovery");}));
  write(manifest,trusted);
#ifndef _WIN32
  auto link=root/"link";fs::create_directory_symlink(backup/"data",link);
  assert(fails([&]{RecoveryCatalog::export_item(records[0],link);}));
#endif
  fs::remove_all(root);
 }catch(...){fs::remove_all(root);throw;}
}
