#include "synchrorep/engine.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <stdexcept>
namespace synchrorep {
namespace {
using Table=std::map<std::string,Entry>;
struct Pair { Entry left, right; };
using History=std::map<std::string,Pair>;
bool same(const Entry& a,const Entry& b) {
 return a.exists==b.exists && (!a.exists || (a.directory==b.directory && (a.directory || (a.size==b.size && a.modified==b.modified))));
}
Entry info(const fs::path& p) {
 Entry e;
 std::error_code ec;
 auto status=fs::symlink_status(p,ec);
 if(ec || status.type()==fs::file_type::not_found) return e;
 if(fs::is_symlink(status)) throw std::runtime_error("Symbolic links are not synchronized: "+p.string());
 if(!fs::is_directory(status) && !fs::is_regular_file(status)) throw std::runtime_error("Unsupported filesystem entry: "+p.string());
 e.exists=true;e.directory=fs::is_directory(status);
 if(!e.directory) e.size=fs::file_size(p);
 e.modified=static_cast<std::int64_t>(fs::last_write_time(p).time_since_epoch().count());
 return e;
}
Table scan(const fs::path& root) {
 Table t;
 if(!fs::is_directory(root)) throw std::runtime_error("Missing source directory: "+root.string());
 for(auto it=fs::recursive_directory_iterator(root,fs::directory_options::none);it!=fs::recursive_directory_iterator();++it){
  auto relative=it->path().lexically_relative(root).generic_string();
  t.emplace(relative,info(it->path()));
 }
 return t;
}
History load(const fs::path& file) {
 History result; std::ifstream in(file); if(!in) return result;
 std::string header;std::getline(in,header);
 if(header!="SYNCHROREP1") throw std::runtime_error("Incompatible synchronization history");
 std::string path; Pair p;int le,ld,re,rd;
 while(in>>std::quoted(path)>>le>>ld>>p.left.size>>p.left.modified>>re>>rd>>p.right.size>>p.right.modified){
  p.left.exists=le;p.left.directory=ld;p.right.exists=re;p.right.directory=rd;
  result[path]=p;
 }
 if(!in.eof()) throw std::runtime_error("Damaged synchronization history");
 return result;
}
void save(const fs::path& file,const History& history) {
 fs::create_directories(file.parent_path());
 auto temp=file;temp+=".tmp";
 { std::ofstream out(temp,std::ios::trunc);if(!out)throw std::runtime_error("Cannot write synchronization history");
 out<<"SYNCHROREP1\n";
 for(const auto& [path,p]:history)
  out<<std::quoted(path)<<' '<<p.left.exists<<' '<<p.left.directory<<' '<<p.left.size<<' '<<p.left.modified<<' '<<p.right.exists<<' '<<p.right.directory<<' '<<p.right.size<<' '<<p.right.modified<<'\n';
 out.flush();if(!out)throw std::runtime_error("Cannot flush history");
 }
 // Replace existing state only after successfully persisting the temporary file.
 std::error_code ec;fs::rename(temp,file,ec);
 if(ec) {fs::remove(file,ec);fs::rename(temp,file);}
}
Action decide(const Entry& l,const Entry& r,const Pair& old) {
 const bool cl=!same(l,old.left),cr=!same(r,old.right);
 if(!cl&&!cr)return Action::None;
 if(cl&&cr) {
  if(!l.exists&&!r.exists)return Action::None;
  if(l.exists&&r.exists&&same(l,r))return Action::None;
  return Action::Conflict;
 }
 if(cl) return l.exists?Action::CopyLeftToRight:Action::DeleteRight;
 return r.exists?Action::CopyRightToLeft:Action::DeleteLeft;
}
std::vector<Decision> build(const Table& l,const Table& r,const History& old) {
 std::set<std::string> names;
 for(auto& [p,_]:l)names.insert(p);
 for(auto& [p,_]:r)names.insert(p);
 for(auto& [p,_]:old)names.insert(p);
 std::vector<Decision> out;
 for(const auto& name:names) {
  const auto a=l.find(name); const auto b=r.find(name); const auto o=old.find(name);
  const Entry left=a==l.end()?Entry{}:a->second,right=b==r.end()?Entry{}:b->second;
  const Pair previous=o==old.end()?Pair{}:o->second;
  auto act=decide(left,right,previous);
  if((left.exists&&right.exists&&left.directory!=right.directory))act=Action::Conflict;
  out.push_back({name,act});
 }
 return out;
}
void copy_entry(const fs::path& source,const fs::path& target) {
 auto type=info(source);
 if(type.directory) {fs::create_directories(target);return;}
 fs::create_directories(target.parent_path());
 // Copy to sibling temporary file, then promote it.
 auto tmp=target;tmp+=".synchrorep-tmp";
 fs::copy_file(source,tmp,fs::copy_options::overwrite_existing);
 fs::last_write_time(tmp,fs::last_write_time(source));
 std::error_code ec;fs::rename(tmp,target,ec);
 if(ec){fs::remove(target,ec);fs::rename(tmp,target);}
}
}
Engine::Engine(fs::path state):state_(std::move(state)){}
std::vector<Decision> Engine::plan(const fs::path& left,const fs::path& right)const {
 return build(scan(left),scan(right),load(state_));
}
Result Engine::run(const fs::path& left,const fs::path& right,bool dry_run) {
 if(fs::equivalent(left,right))throw std::runtime_error("Cannot synchronize a folder with itself");
 const auto ls=scan(left); const auto rs=scan(right); const auto old=load(state_);
 auto decisions=build(ls,rs,old);Result result;
 // Conflicts are never resolved by automatic timestamp preference.
 for(const auto& d:decisions)if(d.action==Action::Conflict)result.conflicts.push_back(d.relative);
 if(!result.conflicts.empty())return result; // transaction: do not touch any file
 if(dry_run)return result;
 // Create directories and copy files first, then remove deleted entries deepest first.
 for(const auto& d:decisions){
  if(d.action==Action::CopyLeftToRight){copy_entry(left/d.relative,right/d.relative);result.copied++;}
  if(d.action==Action::CopyRightToLeft){copy_entry(right/d.relative,left/d.relative);result.copied++;}
 }
 std::reverse(decisions.begin(),decisions.end());
 for(const auto& d:decisions){
  if(d.action==Action::DeleteLeft){fs::remove(left/d.relative);result.removed++;}
  if(d.action==Action::DeleteRight){fs::remove(right/d.relative);result.removed++;}
 }
 auto l2=scan(left),r2=scan(right);History next;
 std::set<std::string> names;
 for(const auto& [p,_]:l2)names.insert(p);
 for(const auto& [p,_]:r2)names.insert(p);
 for(const auto& path:names)next[path]={l2.count(path)?l2.at(path):Entry{},r2.count(path)?r2.at(path):Entry{}};
 save(state_,next);return result;
}
}
