#include "synchrorep/engine.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace synchrorep {
namespace {
struct Pair {Entry left,right;};
using History=std::map<std::string,Pair>;
struct State {std::string left,right;History history;};

bool same(const Entry& a,const Entry& b){
 return a.exists==b.exists && (!a.exists || (a.directory==b.directory &&
 (a.directory || (a.size==b.size && a.modified==b.modified))));
}
bool equal_bytes(const Endpoint& left,const Endpoint& right,const std::string& name,
                 const Entry& l,const Entry& r) {
 if(!l.exists||!r.exists||l.directory!=r.directory)return false;
 if(l.directory)return true;
 if(l.size!=r.size)return false;
 auto a=left.read(name),b=right.read(name);
 std::array<char,65536> aa{},bb{};
 do {
  a->read(aa.data(),aa.size());b->read(bb.data(),bb.size());
  auto n=a->gcount(),m=b->gcount();
  if(a->bad()||b->bad()||n!=m)return false;
  if(!std::equal(aa.begin(),aa.begin()+n,bb.begin()))return false;
  if(n==0)break;
 }while(true);
 return true;
}
State load(const fs::path& file,const Endpoint& left,const Endpoint& right) {
 State result{left.identity(),right.identity(),{}};
 std::error_code ec;
 if(!fs::exists(file,ec)) {
  if(ec)throw std::runtime_error("Cannot access synchronization history");
  return result;
 }
 std::ifstream in(file,std::ios::binary);
 if(!in)throw std::runtime_error("Cannot read synchronization history");
 std::string header;
 std::getline(in,header);
 if(header!="SYNCHROREP3")throw std::runtime_error("Unsupported history format (migration required)");
 std::string a,b;in>>std::quoted(a)>>std::quoted(b);
 if(!in || a!=result.left || b!=result.right)
  throw std::runtime_error("Synchronization history belongs to a different pair of locations");
 std::string name;int le,ld,re,rd;
 Pair item;
 while(in>>std::quoted(name)>>le>>ld>>item.left.size>>item.left.modified
          >>re>>rd>>item.right.size>>item.right.modified) {
  if(name.empty() || name.front()=='/' || name.find('\\')!=std::string::npos ||
     fs::path(name).is_absolute() || fs::path(name).has_root_name() ||
     name==".." || name.rfind("../",0)==0 ||
     name.find("/../")!=std::string::npos ||
     (name.size()>=3 && name.compare(name.size()-3,3,"/..") == 0))
   throw std::runtime_error("Invalid path in history");
  item.left.exists=le!=0;item.left.directory=ld!=0;
  item.right.exists=re!=0;item.right.directory=rd!=0;
  result.history.emplace(name,item);
 }
 if(!in.eof())throw std::runtime_error("Invalid/truncated synchronization history");
 return result;
}
void save(const fs::path& file,const State& state) {
 auto parent=file.parent_path();
 if(!parent.empty())fs::create_directories(parent);
 auto tmp=file;tmp+=".tmp";auto backup=file;backup+=".bak";
 {
  std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
  if(!out)throw std::runtime_error("Cannot create synchronization history");
  out<<"SYNCHROREP3\n"<<std::quoted(state.left)<<' '<<std::quoted(state.right)<<'\n';
  for(const auto& [name,p]:state.history)
   out<<std::quoted(name)<<' '<<p.left.exists<<' '<<p.left.directory<<' '
      <<p.left.size<<' '<<p.left.modified<<' '
      <<p.right.exists<<' '<<p.right.directory<<' '
      <<p.right.size<<' '<<p.right.modified<<'\n';
  out.flush();if(!out)throw std::runtime_error("Cannot flush synchronization history");
 }
 std::error_code ec;
 fs::rename(tmp,file,ec);
 if(!ec)return;
 // Some Windows filesystems require a separate rename when replacing an
 // existing file. Keep a rollback copy rather than deleting the old state.
 if(!fs::exists(file))throw fs::filesystem_error("Cannot save history",tmp,file,ec);
 fs::remove(backup,ec);
 fs::rename(file,backup);
 try{fs::rename(tmp,file);}catch(...){fs::rename(backup,file);throw;}
 fs::remove(backup,ec);
}
Action compare(const Entry& l,const Entry& r,const Pair& old,
               const Endpoint& left,const Endpoint& right,const std::string& name,
               const Options& options){
 const bool cl=!same(l,old.left),cr=!same(r,old.right);
 if(l.exists&&r.exists&&l.directory!=r.directory)return Action::Conflict;
 if(!cl&&!cr)return Action::None;
 if(cl&&cr){
  if(!l.exists&&!r.exists)return Action::None;
  if(l.exists&&r.exists&&equal_bytes(left,right,name,l,r))return Action::None;
  if(l.exists&&r.exists&&l.directory&&r.directory)return Action::None;
  if(!l.exists||!r.exists)return Action::Conflict;
  if(options.conflict==ConflictPolicy::Newest && l.modified!=r.modified)
   return l.modified>r.modified?Action::CopyLeftToRight:Action::CopyRightToLeft;
  if(options.conflict==ConflictPolicy::Oldest && l.modified!=r.modified)
   return l.modified<r.modified?Action::CopyLeftToRight:Action::CopyRightToLeft;
  return Action::Conflict;
 }
 if(cl){
  if(l.exists)return Action::CopyLeftToRight;
  return options.propagate_deletions?Action::DeleteRight:Action::CopyRightToLeft;
 }
 if(r.exists)return Action::CopyRightToLeft;
 return options.propagate_deletions?Action::DeleteLeft:Action::CopyLeftToRight;
}
std::vector<Decision> build(const Endpoint& left,const Endpoint& right,
                            const Snapshot& l,const Snapshot& r,
                            const History& old,const Options& options) {
 std::set<std::string> names;
 for(const auto& [name,_]:l)names.insert(name);
 for(const auto& [name,_]:r)names.insert(name);
 for(const auto& [name,_]:old)names.insert(name);
 std::vector<Decision> out;
 for(const auto& name:names) {
  const auto li=l.find(name); const auto ri=r.find(name); const auto oi=old.find(name);
  const Entry a=li==l.end()?Entry{}:li->second;
  const Entry b=ri==r.end()?Entry{}:ri->second;
  const Pair oldPair=oi==old.end()?Pair{}:oi->second;
  out.push_back({name,compare(a,b,oldPair,left,right,name,options)});
 }
 return out;
}
bool is_nested(const fs::path& a,const fs::path& b){
 auto i=a.begin(),j=b.begin();
 while(i!=a.end()&&j!=b.end()&&*i==*j){++i;++j;}
 return i==a.end()||j==b.end();
}
void ensure_distinct(const Endpoint& left,const Endpoint& right,const fs::path& state) {
 if(left.identity()==right.identity())throw std::runtime_error("Endpoints must differ");
 auto l=dynamic_cast<const LocalEndpoint*>(&left);
 auto r=dynamic_cast<const LocalEndpoint*>(&right);
 if(l&&r){
  auto a=fs::weakly_canonical(l->root()),b=fs::weakly_canonical(r->root());
  if(is_nested(a,b))throw std::runtime_error("Nested synchronization directories are unsafe");
  auto s=fs::absolute(state).lexically_normal();
  if(is_nested(a,s)||is_nested(b,s))throw std::runtime_error("History must be outside synchronized directories");
 }
}
void transfer(Endpoint& from,Endpoint& to,const std::string& name,const Entry& info){
 if(info.directory)to.make_directory(name);
 else{auto data=from.read(name);to.write(name,*data,info);}
}
void erase(Endpoint& endpoint,const std::string& name){endpoint.remove(name);}
}
Engine::Engine(fs::path state,Options options):state_(std::move(state)),options_(options){}
std::vector<Decision> Engine::plan(const Endpoint& left,const Endpoint& right)const{
 ensure_distinct(left,right,state_);
 auto state=load(state_,left,right);
 return build(left,right,left.scan(),right.scan(),state.history,options_);
}
std::vector<Decision> Engine::plan(const fs::path& left,const fs::path& right)const{
 LocalEndpoint l(left),r(right);return plan(l,r);
}
Result Engine::run(Endpoint& left,Endpoint& right,bool dry_run){
 ensure_distinct(left,right,state_);
 auto state=load(state_,left,right);
 const auto leftScan=left.scan(),rightScan=right.scan();
 auto decisions=build(left,right,leftScan,rightScan,state.history,options_);
 Result result;
 for(const auto& decision:decisions)
  if(decision.action==Action::Conflict)result.conflicts.push_back(decision.relative);
 if(dry_run)return result;
 if(!result.conflicts.empty() && options_.conflict!=ConflictPolicy::Ignore)return result;
 // No filesystem modifications until both scans and every conflict have
 // been evaluated. Conflicting entries are not committed to the history.
 for(const auto& d:decisions) {
  const auto li=leftScan.find(d.relative),ri=rightScan.find(d.relative);
  switch(d.action){
   case Action::CopyLeftToRight: transfer(left,right,d.relative,li->second);++result.copied;break;
   case Action::CopyRightToLeft: transfer(right,left,d.relative,ri->second);++result.copied;break;
   default:break;
  }
 }
 std::reverse(decisions.begin(),decisions.end());
 for(const auto& d:decisions) {
  if(d.action==Action::DeleteLeft){erase(left,d.relative);++result.removed;}
  if(d.action==Action::DeleteRight){erase(right,d.relative);++result.removed;}
 }
 auto l2=left.scan(),r2=right.scan();
 for(const auto& d:decisions){
  if(d.action==Action::Conflict)continue;
  const auto li=l2.find(d.relative),ri=r2.find(d.relative);
  Pair p{li==l2.end()?Entry{}:li->second,ri==r2.end()?Entry{}:ri->second};
  if(!p.left.exists&&!p.right.exists)state.history.erase(d.relative);
  else state.history[d.relative]=p;
 }
 save(state_,state);
 return result;
}
Result Engine::run(const fs::path& left,const fs::path& right,bool dry_run){
 LocalEndpoint l(left),r(right);return run(l,r,dry_run);
}
}
