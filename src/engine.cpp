// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/engine.hpp"
#ifdef SYNCHROREP_SQLITE_ENABLED
#include "synchrorep/sqlite_history.hpp"
#endif
#include "synchrorep/sha256.hpp"
#include "synchrorep/recovery_store.hpp"
#include "synchrorep/run_log.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <atomic>
#include <chrono>
#include <memory>
#include <cerrno>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace synchrorep {
namespace {
struct Pair {Entry left,right;};
using History=std::map<std::string,Pair>;
struct State {std::string left,right;History history;};

bool same(const Entry& a,const Entry& b){
 if(a.exists!=b.exists)return false;
 if(!a.exists)return true;
 if(a.directory!=b.directory)return false;
 if(a.directory)return true;
 if(a.size!=b.size)return false;
 // Content is authoritative when both scans carry a digest.
 if(!a.hash.empty()&&!b.hash.empty())return a.hash==b.hash;
 return a.modified==b.modified;
}
void check_cancel(const RunCallbacks& callbacks){
 if(callbacks.cancelled && callbacks.cancelled())throw RunCancelled();
}
struct CancellationBinding {
 const Endpoint& left;
 const Endpoint& right;
 CancellationBinding(const Endpoint& a,const Endpoint& b,const RunCallbacks& callbacks)
 :left(a),right(b){
  left.set_cancellation(callbacks.cancelled);
  right.set_cancellation(callbacks.cancelled);
 }
 ~CancellationBinding(){
  left.set_cancellation({});
  right.set_cancellation({});
 }
};
void emit(const RunCallbacks& callbacks,Phase phase,std::size_t done,std::size_t total,const std::string& name={}){
 if(callbacks.progress)callbacks.progress(Progress{phase,done,total,name});
}
void hash_common(Snapshot& leftScan, Snapshot& rightScan,
                 const Endpoint& left, const Endpoint& right,
                 const RunCallbacks& callbacks, bool enabled) {
 if(!enabled)return;
 for(auto& [name, entry]:leftScan){
  check_cancel(callbacks);
  auto it=rightScan.find(name);
  if(it==rightScan.end()||entry.directory||it->second.directory)continue;
  auto leftStream=left.read(name);
  auto rightStream=right.read(name);
  entry.hash=sha256(*leftStream,entry.size);
  it->second.hash=sha256(*rightStream,it->second.size);
 }
}
Snapshot scan_entries(const Endpoint& endpoint,const RunCallbacks& callbacks,Phase phase){
 check_cancel(callbacks);
 auto entries=endpoint.scan();
 emit(callbacks,phase,0,entries.size());
 std::size_t done=0;
 for(const auto& [name,entry]:entries){
  (void)entry;
  check_cancel(callbacks);
  emit(callbacks,phase,++done,entries.size(),name);
 }
 check_cancel(callbacks);
 return entries;
}
State load_legacy(const fs::path& file,const Endpoint& left,const Endpoint& right) {
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
 if(header!="SYNCHROREP4")throw std::runtime_error("Unsupported history format (migration required)");
 std::string a,b;in>>std::quoted(a)>>std::quoted(b);
 if(!in || a!=result.left || b!=result.right)
  throw std::runtime_error("Synchronization history belongs to a different pair of locations");
 std::string name;int le,ld,re,rd;
 Pair item;
 while(in>>std::quoted(name)>>le>>ld>>item.left.size>>item.left.modified>>std::quoted(item.left.hash)
          >>re>>rd>>item.right.size>>item.right.modified>>std::quoted(item.right.hash)) {
  if(name.empty() || name.front()=='/' || name.find('\\')!=std::string::npos ||
     fs::path(name).is_absolute() || fs::path(name).has_root_name() ||
     name==".." || name.rfind("../",0)==0 ||
     name.find("/../")!=std::string::npos ||
     (name.size()>=3 && name.compare(name.size()-3,3,"/..") == 0))
   throw std::runtime_error("Invalid path in history");
  auto valid_hash=[](const Entry& e){
   if(!e.exists||e.directory)return e.hash.empty();
   return (e.hash.empty()||e.hash.size()==64)&&
     std::all_of(e.hash.begin(),e.hash.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
  };
  item.left.exists=le!=0;item.left.directory=ld!=0;
  item.right.exists=re!=0;item.right.directory=rd!=0;
  if(!valid_hash(item.left)||!valid_hash(item.right))
    throw std::runtime_error("Invalid SHA-256 fingerprint in history");
  if(!result.history.emplace(name,item).second)
    throw std::runtime_error("Duplicate synchronization history path");
 }
 if(!in.eof())throw std::runtime_error("Invalid/truncated synchronization history");
 return result;
}
fs::path sqlite_path(const fs::path& legacy){
 auto path=legacy;path+=".sqlite";
 return path;
}
State load(const fs::path& file,const Endpoint& left,const Endpoint& right){
#ifdef SYNCHROREP_SQLITE_ENABLED
 SqliteHistory store(sqlite_path(file));
 if(store.has_pair(left.identity(),right.identity())){
  State result{left.identity(),right.identity(),{}};
  for(const auto& [name,item]:store.load(result.left,result.right))
   result.history.emplace(name,Pair{item.left,item.right});
  return result;
 }
 if(store.has_any_pair())throw std::runtime_error("Synchronization history belongs to a different pair of locations");
#endif
 return load_legacy(file,left,right);
}
void sync_file(const fs::path& file){
#ifdef _WIN32
 HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,
  FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
  FILE_ATTRIBUTE_NORMAL,nullptr);
 if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open journal for durable flush");
 const BOOL done=FlushFileBuffers(handle);
 CloseHandle(handle);
 if(!done)throw std::runtime_error("Cannot flush journal to disk");
#else
 int fd=open(file.c_str(),O_RDONLY);
 if(fd<0)throw fs::filesystem_error("Cannot reopen file for fsync",file,
  std::error_code(errno,std::generic_category()));
 int code=fsync(fd);
 int failure=errno;
 close(fd);
 if(code!=0)throw fs::filesystem_error("fsync failed",file,
  std::error_code(failure,std::generic_category()));
#endif
}
void sync_parent(const fs::path& file){
#ifndef _WIN32
 auto parent=file.parent_path();
 if(parent.empty())parent=".";
 int fd=open(parent.c_str(),O_RDONLY|O_DIRECTORY);
 if(fd<0)throw fs::filesystem_error("Cannot open journal directory",parent,
  std::error_code(errno,std::generic_category()));
 int code=fsync(fd);
 int failure=errno;
 close(fd);
 if(code!=0)throw fs::filesystem_error("Directory fsync failed",parent,
  std::error_code(failure,std::generic_category()));
#else
 (void)file;
#endif
}
fs::path journal_path(const fs::path& state){
 auto file=state;
 file+=".journal";
 return file;
}
void validate_journal(const fs::path& file,const Endpoint& left,const Endpoint& right){
 std::ifstream input(file,std::ios::binary);
 if(!input)throw std::runtime_error("Cannot read recovery journal");
 std::string header,a,b;
 std::getline(input,header);
 if(header!="SYNCHROREP-RUN1")throw std::runtime_error("Invalid recovery journal format");
 if(!(input>>std::quoted(a)>>std::quoted(b))||
    a!=left.identity()||b!=right.identity())
  throw std::runtime_error("Recovery journal belongs to different endpoints");
 std::string line;
 std::getline(input,line); // consume remainder of identity line
 while(std::getline(input,line)){
  if(line.empty())throw std::runtime_error("Empty recovery journal entry");
  std::istringstream record(line);
  std::string action,name,extra;
  if(!(record>>action>>std::quoted(name))||(action!="BEGIN"&&action!="DONE")||
     !(record>>std::ws).eof()||name.empty())
   throw std::runtime_error("Damaged recovery journal entry");
 }
 if(input.bad())throw std::runtime_error("Cannot finish reading recovery journal");
}
// A completed journal contains exactly one DONE for each BEGIN.
// An unfinished operation must never be silently recovered.
std::set<std::string> journal_unfinished_operations(const fs::path& file){
 std::ifstream input(file,std::ios::binary);
 if(!input)throw std::runtime_error("Cannot read recovery journal");
 std::string line;
 std::getline(input,line); // format
 std::getline(input,line); // identities validated by caller
 std::map<std::string,int> pending;
 while(std::getline(input,line)){
  std::istringstream record(line);
  std::string action,name;
  if(!(record>>action>>std::quoted(name)) || !(record>>std::ws).eof())
   throw std::runtime_error("Damaged recovery journal entry");
  if(action=="BEGIN")++pending[name];
  else if(action=="DONE"){
   auto it=pending.find(name);
   if(it==pending.end()||it->second==0)throw std::runtime_error("Unmatched recovery completion");
   --it->second;
  }else throw std::runtime_error("Damaged recovery journal action");
 }
 if(input.bad())throw std::runtime_error("Cannot finish reading recovery journal");
 std::set<std::string> result;
 for(const auto& [name,count]:pending)if(count)result.insert(name);
 return result;
}
// An interrupted atomic write can leave a partial .synchrorep-write-N.tmp.
// Only files corresponding to unfinished operations in a validated journal may
// be discarded. Never remove an existing destination or unrelated files.
void clear_interrupted_writes(const fs::path& journal,const Endpoint& left,const Endpoint& right){
 const auto unfinished=journal_unfinished_operations(journal);
 for(const auto* endpoint:{&left,&right}){
  const auto* local=dynamic_cast<const LocalEndpoint*>(endpoint);
  if(!local)continue;
  for(const auto& name:unfinished){
   if(name.empty()||name.find('\\')!=std::string::npos)throw std::runtime_error("Unsafe journal path");
   const auto path=fs::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()),name.size()));
   if(path.is_absolute()||path.has_root_name())throw std::runtime_error("Unsafe journal path");
   auto parent=local->root();
   for(const auto& component:path.parent_path()){
    if(component=="."||component=="..")throw std::runtime_error("Unsafe journal path");
    parent/=component;
    const auto status=fs::symlink_status(parent);
    if(fs::is_symlink(status))throw std::runtime_error("Journal path contains symbolic link");
    if(status.type()==fs::file_type::not_found)break;
   }
   if(path.filename()=="."||path.filename()==".."||path.filename().empty())
    throw std::runtime_error("Unsafe journal path");
   if(!fs::is_directory(parent))continue;
   const auto prefix=path.filename().native()+fs::path(".synchrorep-write-").native();
   const auto suffix=fs::path(".tmp").native();
   for(const auto& item:fs::directory_iterator(parent)){
    const auto filename=item.path().filename().native();
    if(filename.size()<=prefix.size()+suffix.size()||
       filename.compare(0,prefix.size(),prefix)!=0||
       filename.compare(filename.size()-suffix.size(),suffix.size(),suffix)!=0)
     continue;
    const auto digits=filename.substr(prefix.size(),filename.size()-prefix.size()-suffix.size());
    if(!std::all_of(digits.begin(),digits.end(),[](auto c){return c>='0'&&c<='9';}))
     continue;
    if(!fs::is_regular_file(item.symlink_status()))
     throw std::runtime_error("Interrupted transfer artifact is not a regular file");
    if(!fs::remove(item.path()))throw std::runtime_error("Cannot remove interrupted transfer artifact");
   }
  }
 }
}
void begin_journal(const fs::path& file,const Endpoint& left,const Endpoint& right,
                   const std::vector<Decision>& decisions){
 auto parent=file.parent_path();
 if(!parent.empty())fs::create_directories(parent);
 auto temporary=file;temporary+=".tmp";
 {
  std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
  if(!out)throw std::runtime_error("Cannot create recovery journal");
  out<<"SYNCHROREP-RUN1\n"<<std::quoted(left.identity())<<' '<<std::quoted(right.identity())<<'\n';
  for(const auto& decision:decisions)
   if(decision.action!=Action::None&&decision.action!=Action::Conflict&&decision.action!=Action::Skip)
    out<<"BEGIN "<<std::quoted(decision.relative)<<'\n';
  out.flush();
  if(!out)throw std::runtime_error("Cannot write recovery journal");
 }
 sync_file(temporary);
 fs::rename(temporary,file);
 sync_parent(file);
}
void journal_completed(const fs::path& file,const std::string& relative){
 std::ofstream out(file,std::ios::binary|std::ios::app);
 if(!out)throw std::runtime_error("Recovery journal inaccessible during operation");
 out<<"DONE "<<std::quoted(relative)<<'\n';
 out.flush();
 if(!out)throw std::runtime_error("Failed updating recovery journal");
 out.close();
 sync_file(file);
}
#ifndef SYNCHROREP_SQLITE_ENABLED
void save_legacy(const fs::path& file,const State& state) {
 auto parent=file.parent_path();
 if(!parent.empty())fs::create_directories(parent);
 auto tmp=file;tmp+=".tmp";auto backup=file;backup+=".bak";
 {
  std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
  if(!out)throw std::runtime_error("Cannot create synchronization history");
  out<<"SYNCHROREP4\n"<<std::quoted(state.left)<<' '<<std::quoted(state.right)<<'\n';
  for(const auto& [name,p]:state.history)
   out<<std::quoted(name)<<' '<<p.left.exists<<' '<<p.left.directory<<' '
      <<p.left.size<<' '<<p.left.modified<<' '<<std::quoted(p.left.hash)<<' '
      <<p.right.exists<<' '<<p.right.directory<<' '
      <<p.right.size<<' '<<p.right.modified<<' '<<std::quoted(p.right.hash)<<'\n';
  out.flush();if(!out)throw std::runtime_error("Cannot flush synchronization history");
 }
 sync_file(tmp);
 std::error_code ec;
 fs::rename(tmp,file,ec);
 if(!ec){sync_parent(file);return;}
 // Some Windows filesystems require a separate rename when replacing an
 // existing file. Keep a rollback copy rather than deleting the old state.
 if(!fs::exists(file))throw fs::filesystem_error("Cannot save history",tmp,file,ec);
 fs::remove(backup,ec);
 fs::rename(file,backup);
 try{fs::rename(tmp,file);}catch(...){fs::rename(backup,file);throw;}
 fs::remove(backup,ec);
 sync_parent(file);
}
#endif
void save(const fs::path& file,const State& state){
#ifdef SYNCHROREP_SQLITE_ENABLED
 SqliteHistory store(sqlite_path(file));
 StoredHistory rows;
 for(const auto& [name,item]:state.history)
  rows.emplace(name,StoredPair{item.left,item.right});
 store.save(state.left,state.right,rows);
#else
 save_legacy(file,state);
#endif
}
Action compare(const Entry& l,const Entry& r,const Pair& old,
               const Options& options){
 const bool cl=!same(l,old.left),cr=!same(r,old.right);
 // Hash is a change detector only. Prefer the newest source when both
 // endpoints disagree. An exact timestamp tie is a conflict, not a guess.
 if(l.exists&&r.exists&&!l.directory&&!r.directory &&
    !l.hash.empty()&&!r.hash.empty()&&l.hash!=r.hash){
  if(l.modified==r.modified)return Action::Conflict;
  return l.modified>r.modified?Action::CopyLeftToRight:Action::CopyRightToLeft;
 }
 if(l.exists&&r.exists&&l.directory!=r.directory)return Action::Conflict;
 if(!cl&&!cr)return Action::None;
 // Without digests, compare size and mtime; the newest existing copy wins.
 // A digest match above already identifies identical content regardless of time.
 if(l.exists&&r.exists&&!l.directory&&!r.directory&&
    l.modified!=r.modified&&!same(l,r))
  return l.modified>r.modified?Action::CopyLeftToRight:Action::CopyRightToLeft;
 if(cl&&cr){
  if(!l.exists&&!r.exists)return Action::None;
  if(l.exists&&r.exists&&same(l,r))return Action::None;
  if(l.exists&&r.exists&&l.directory&&r.directory)return Action::None;
  if(!l.exists||!r.exists)return Action::Conflict;
  if(l.modified!=r.modified)
   return l.modified>r.modified?Action::CopyLeftToRight:Action::CopyRightToLeft;
  return Action::Conflict;
 }
 if(cl){
  if(l.exists)return Action::CopyLeftToRight;
  return options.propagate_deletions?Action::DeleteRight:Action::CopyRightToLeft;
 }
 if(r.exists)return Action::CopyRightToLeft;
 return options.propagate_deletions?Action::DeleteLeft:Action::CopyLeftToRight;
}
std::vector<Decision> mirror_decisions(const Snapshot& source,const Snapshot& target,
                                      const History& old,const Options& options){
 // Fail closed if a newly configured/empty source would wipe an existing target.
 // Deliberately deleting the last source file is allowed only after a prior
 // successful mirror with at least one source entry in the history.
 bool previously_nonempty=false;
 for(const auto& [_,pair]:old)if(pair.left.exists){previously_nonempty=true;break;}
 if(source.empty()&&!target.empty()&&options.propagate_deletions&&!previously_nonempty)
  throw std::runtime_error("Mirror source is empty and has no established history; refusing target deletion");
 std::set<std::string> paths;
 for(const auto& [name,_]:source)paths.insert(name);
 for(const auto& [name,_]:target)paths.insert(name);
 std::vector<Decision> result;
 for(const auto& name:paths){
  const auto a=source.find(name),b=target.find(name);
  if(a==source.end()){
   result.push_back({name,options.propagate_deletions?Action::DeleteRight:Action::None});
  }else if(b==target.end()){
   result.push_back({name,Action::CopyLeftToRight});
  }else if(a->second.directory!=b->second.directory){
   result.push_back({name,Action::Conflict}); // require explicit manual resolution
  }else{
   result.push_back({name,same(a->second,b->second)?Action::None:Action::CopyLeftToRight});
  }
 }
 return result;
}
std::vector<Decision> build(const Snapshot& l,const Snapshot& r,
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
  auto action=compare(a,b,oldPair,options);
  if(action==Action::Conflict||options.overrides.count(name)) {
   auto override=options.overrides.find(name);
   if(override!=options.overrides.end()){
    // Never replace a directory with a file (or vice versa) implicitly.
    // Such conflicts need explicit filesystem intervention.
    if(a.exists&&b.exists&&a.directory!=b.directory) {
     if(override->second==ConflictChoice::Skip)action=Action::Skip;
    }else{
     switch(override->second){
      case ConflictChoice::KeepLeft:
       action=a.exists?Action::CopyLeftToRight:Action::DeleteRight;
       break;
      case ConflictChoice::KeepRight:
       action=b.exists?Action::CopyRightToLeft:Action::DeleteLeft;
       break;
      case ConflictChoice::Skip:action=Action::Skip;break;
     }
    }
   }
  }
  out.push_back({name,action});
 }
 return out;
}
bool same_path_component(const fs::path& left,const fs::path& right){
#ifdef _WIN32
 // Windows paths are case insensitive, including drive letters; std::path
 // operator== is lexical and cannot be used for this safety boundary.
 const auto a=left.native(),b=right.native();
 const auto result=CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE);
 if(result==0)throw std::runtime_error("Cannot compare Windows path components");
 return result==CSTR_EQUAL;
#else
 return left==right;
#endif
}
bool is_nested(const fs::path& a,const fs::path& b){
 auto i=a.begin(),j=b.begin();
 while(i!=a.end()&&j!=b.end()&&same_path_component(*i,*j)){++i;++j;}
 return i==a.end()||j==b.end();
}
void ensure_distinct(const Endpoint& left,const Endpoint& right,const fs::path& state) {
 if(left.identity()==right.identity())throw std::runtime_error("Endpoints must differ");
 auto l=dynamic_cast<const LocalEndpoint*>(&left);
 auto r=dynamic_cast<const LocalEndpoint*>(&right);
 if(l&&r){
  auto a=canonical_endpoint_path(l->root()),b=canonical_endpoint_path(r->root());
  if(is_nested(a,b))throw std::runtime_error("Nested synchronization directories are unsafe");
  auto s=canonical_endpoint_path(state);
  if(is_nested(a,s)||is_nested(b,s))throw std::runtime_error("History must be outside synchronized directories");
 }
}
void transfer(Endpoint& from,Endpoint& to,const std::string& name,const Entry& info){
 if(info.directory)to.make_directory(name);
 else{auto data=from.read(name);to.write(name,*data,info);}
}
bool erase(Endpoint& endpoint,const std::string& name,bool recycle){
 if(recycle&&endpoint.supports_recycle_bin()){
  endpoint.recycle(name);
  return true;
 }
 endpoint.remove(name);
 return false;
}
std::string unique_run_id(){
 static std::atomic<unsigned long long> serial{0};
 return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
   +"-"+std::to_string(++serial);
}
void ensure_external(const fs::path& archive,const Endpoint& left,const Endpoint& right){
 auto location=canonical_endpoint_path(archive);
 for(const auto* endpoint:{&left,&right}){
  auto local=dynamic_cast<const LocalEndpoint*>(endpoint);
  if(local && is_nested(canonical_endpoint_path(local->root()),location))
   throw std::runtime_error("Recovery archive must be outside synchronized roots");
 }
}
}
namespace {
bool wildcard_match(std::string_view pattern,std::string_view path){
 std::vector<unsigned char> memo((pattern.size()+1)*(path.size()+1),0);
 const auto width=path.size()+1;
 memo[pattern.size()*width+path.size()]=1;
 for(std::size_t i=pattern.size();i-->0;){
  for(std::size_t j=path.size()+1;j-->0;){
   const auto pos=i*width+j;
   if(pattern[i]=='*'){
    const bool recursive=i+1<pattern.size()&&pattern[i+1]=='*';
    const auto next=recursive?i+2:i+1;
    memo[pos]=memo[next*width+j] ||
      (j<path.size() && (recursive||path[j]!='/') && memo[pos+1]);
    if(recursive&&next<pattern.size()&&pattern[next]=='/')
     memo[pos] |= memo[(next+1)*width+j];
   }else if(j<path.size() && ((pattern[i]=='?'&&path[j]!='/')||pattern[i]==path[j]))
    memo[pos]=memo[(i+1)*width+j+1];
  }
 }
 return memo[0]!=0;
}
bool excluded(std::string_view path,const std::vector<std::string>& patterns){
 for(const auto& pattern:patterns){
  if(pattern.empty())continue;
  if(pattern.size()>3&&pattern.ends_with("/**")&&
     path==std::string_view(pattern).substr(0,pattern.size()-3))return true;
  const bool basename=pattern.find('/')==std::string::npos;
  auto candidate=path;
  while(true){
   const auto name=basename?candidate.substr(candidate.find_last_of('/')==std::string_view::npos?0:candidate.find_last_of('/')+1):candidate;
   if(wildcard_match(pattern,name))return true;
   auto slash=candidate.rfind('/');
   if(slash==std::string_view::npos)break;
   candidate=candidate.substr(0,slash);
  }
 }
 return false;
}
void filter_exclusions(Snapshot& snapshot,const std::vector<std::string>& patterns){
 for(auto it=snapshot.begin();it!=snapshot.end();)
  if(excluded(it->first,patterns))it=snapshot.erase(it);else ++it;
}
void filter_history(std::map<std::string,Pair>& history,const std::vector<std::string>& patterns){
 for(auto it=history.begin();it!=history.end();)
  if(excluded(it->first,patterns))it=history.erase(it);else ++it;
}
}
Engine::Engine(fs::path state,Options options):state_(std::move(state)),options_(options){
 for(const auto& pattern:options_.exclude_patterns)
  if(pattern.empty()||pattern[0]=='/'||pattern.find('\\')!=std::string::npos||
     pattern.find("..")!=std::string::npos||pattern.size()>256)
   throw std::invalid_argument("Invalid exclusion pattern: "+pattern);
}
bool Engine::has_pending_recovery()const{
 std::error_code error;
 bool exists=fs::exists(journal_path(state_),error);
 if(error)throw std::runtime_error("Cannot inspect recovery journal");
 return exists;
}
std::vector<Decision> Engine::plan(const Endpoint& left,const Endpoint& right,const RunCallbacks& callbacks)const{
 ensure_distinct(left,right,state_);
 CancellationBinding binding(left,right,callbacks);
 auto state=load(state_,left,right);
 auto l=scan_entries(left,callbacks,Phase::ScanLeft);
 auto r=scan_entries(right,callbacks,Phase::ScanRight);
 filter_exclusions(l,options_.exclude_patterns);
 filter_exclusions(r,options_.exclude_patterns);
 filter_history(state.history,options_.exclude_patterns);
 hash_common(l,r,left,right,callbacks,state.history.empty()||options_.verify_hash);

 if(options_.mode==SyncMode::Mirror){
  if(!dynamic_cast<const LocalEndpoint*>(&left))
   throw std::runtime_error("Mirror source must be a local directory");
  return mirror_decisions(l,r,state.history,options_);
 }
 return build(l,r,state.history,options_);
}
std::vector<Decision> Engine::plan(const fs::path& left,const fs::path& right,const RunCallbacks& callbacks)const{
 LocalEndpoint l(left),r(right);return plan(l,r,callbacks);
}
Result Engine::run(Endpoint& left,Endpoint& right,bool dry_run,const RunCallbacks& callbacks){
 ensure_distinct(left,right,state_);
 CancellationBinding binding(left,right,callbacks);
 auto state=load(state_,left,right);
 auto journal=journal_path(state_);
 const bool pendingRecovery=!dry_run&&has_pending_recovery();
 if(pendingRecovery){
  validate_journal(journal,left,right);
  if(options_.resume_interrupted)clear_interrupted_writes(journal,left,right);
 }
 auto leftScan=scan_entries(left,callbacks,Phase::ScanLeft);
 auto rightScan=scan_entries(right,callbacks,Phase::ScanRight);
 filter_exclusions(leftScan,options_.exclude_patterns);
 filter_exclusions(rightScan,options_.exclude_patterns);
 filter_history(state.history,options_.exclude_patterns);
 hash_common(leftScan,rightScan,left,right,callbacks,state.history.empty()||options_.verify_hash);

 if(options_.mode==SyncMode::Mirror&&!dynamic_cast<const LocalEndpoint*>(&left))
  throw std::runtime_error("Mirror source must be local");
 auto decisions=options_.mode==SyncMode::Mirror
   ?mirror_decisions(leftScan,rightScan,state.history,options_)
   :build(leftScan,rightScan,state.history,options_);
 // A fully completed journal with a non-destructive plan is only stale
 // metadata. Allow a regular reconciliation; unfinished operations and
 // deletions still require explicit recovery consent.
 if(pendingRecovery&&!options_.resume_interrupted){
  const bool safePlan=std::all_of(decisions.begin(),decisions.end(),[](const Decision& d){
   return d.action==Action::None||d.action==Action::CopyLeftToRight||
          d.action==Action::CopyRightToLeft;
  });
  const auto unfinished=journal_unfinished_operations(journal);
  const bool safeUnfinished=std::all_of(unfinished.begin(),unfinished.end(),[&](const std::string& name){
   const auto decision=std::find_if(decisions.begin(),decisions.end(),[&](const Decision& d){
    return d.relative==name;
   });
   if(decision==decisions.end())return false;
   if(decision->action==Action::CopyLeftToRight)
    return leftScan.count(name)&&leftScan.at(name).exists&&!rightScan.count(name);
   if(decision->action==Action::CopyRightToLeft)
    return rightScan.count(name)&&rightScan.at(name).exists&&!leftScan.count(name);
   return false;
  });
  if(!safePlan||!safeUnfinished)
   throw std::runtime_error("Previous run interrupted; review the preview and explicitly enable recovery");
 }
 Result result;
 for(const auto& decision:decisions)
  if(decision.action==Action::Conflict)result.conflicts.push_back(decision.relative);
 if(dry_run)return result;
 // Skip empty reconciliation cycles without creating new journals or archives.
 if(options_.mode==SyncMode::Mirror && (!state.history.empty()) &&
    std::all_of(decisions.begin(),decisions.end(),[](const Decision& d){return d.action==Action::None;}))
  return result;
 if(!result.conflicts.empty() &&
    (options_.mode==SyncMode::Mirror||options_.conflict!=ConflictPolicy::Ignore))
  return result;
 check_cancel(callbacks);
 emit(callbacks,Phase::Journal,0,1);
 // A previous log is removed ONLY after validating the pair and explicit consent.
 // No filesystem modifications have happened in this run at this point.
 if(has_pending_recovery()){
  if(!fs::remove(journal))throw std::runtime_error("Cannot replace recovery journal");
  sync_parent(journal);
 }
 begin_journal(journal,left,right,decisions);
 emit(callbacks,Phase::Journal,1,1);
 // The log and recovery folder live beside the state, never inside a root.
 auto state_parent=state_.parent_path();
 if(state_parent.empty())state_parent=".";
 auto recovery_root=state_parent/"recovery"/state_.filename()/unique_run_id();
 if(options_.preserve_changes)ensure_external(recovery_root,left,right);
 RunLog events(state_parent/"logs"/(state_.filename().string()+".jsonl"));
 events.record("begin",{},left.identity()+" <-> "+right.identity());
 std::unique_ptr<RecoveryStore> left_backup,right_backup;
 auto preserve=[&](Endpoint& target,const std::string& name,const Entry& original,
                   const std::string& reason,bool is_left){
  if(!options_.preserve_changes||!original.exists)return;
  auto& slot=is_left?left_backup:right_backup;
  if(!slot)slot=std::make_unique<RecoveryStore>(recovery_root/(is_left?"left":"right"));
  events.record("preserve-begin",name,reason);
  Entry backup=original;
  if(!backup.directory && backup.hash.empty()){auto stream=target.read(name);backup.hash=sha256(*stream,backup.size);}
  slot->preserve(target,name,backup,reason);
  ++result.archived;
  events.record("preserved",name,reason);
 };
 try {
 // No filesystem modifications until both scans and every conflict have
 // been evaluated. Conflicting entries are not committed to the history.
 std::size_t copied=0;
 const auto copies=std::count_if(decisions.begin(),decisions.end(),[](const Decision& d){
  return d.action==Action::CopyLeftToRight||d.action==Action::CopyRightToLeft;
 });
 emit(callbacks,Phase::Copy,0,static_cast<std::size_t>(copies));
 for(const auto& d:decisions) {
  check_cancel(callbacks);
  const auto li=leftScan.find(d.relative),ri=rightScan.find(d.relative);
  switch(d.action){
   case Action::CopyLeftToRight:
    if(ri!=rightScan.end())preserve(right,d.relative,ri->second,"overwrite-right",false);
    events.record("copy-left-to-right",d.relative);
    transfer(left,right,d.relative,li->second);++result.copied;break;
   case Action::CopyRightToLeft:
    if(li!=leftScan.end())preserve(left,d.relative,li->second,"overwrite-left",true);
    events.record("copy-right-to-left",d.relative);
    transfer(right,left,d.relative,ri->second);++result.copied;break;
   default:break;
  }
  if(d.action==Action::CopyLeftToRight||d.action==Action::CopyRightToLeft){
   journal_completed(journal,d.relative);
   emit(callbacks,Phase::Copy,++copied,static_cast<std::size_t>(copies),d.relative);
  }
 }
 std::reverse(decisions.begin(),decisions.end());
 const auto deletions=std::count_if(decisions.begin(),decisions.end(),[](const Decision& d){
  return d.action==Action::DeleteLeft||d.action==Action::DeleteRight;
 });
 std::size_t deleted=0;
 emit(callbacks,Phase::Delete,0,static_cast<std::size_t>(deletions));
 for(const auto& d:decisions) {
  check_cancel(callbacks);
  if(d.action==Action::DeleteLeft){
   auto entry=leftScan.find(d.relative);
   if(entry==leftScan.end())throw std::runtime_error("Deletion target vanished: "+d.relative);
   preserve(left,d.relative,entry->second,"delete-left",true);
   events.record("delete-left",d.relative);
   if(erase(left,d.relative,options_.recycle_local_deletions)){
    ++result.recycled;events.record("recycle-left",d.relative);
   }
   ++result.removed;
  }
  if(d.action==Action::DeleteRight){
   auto entry=rightScan.find(d.relative);
   if(entry==rightScan.end())throw std::runtime_error("Deletion target vanished: "+d.relative);
   preserve(right,d.relative,entry->second,"delete-right",false);
   events.record("delete-right",d.relative);
   if(erase(right,d.relative,options_.recycle_local_deletions)){
    ++result.recycled;events.record("recycle-right",d.relative);
   }
   ++result.removed;
  }
  if(d.action==Action::DeleteRight||d.action==Action::DeleteLeft){
   journal_completed(journal,d.relative);
   emit(callbacks,Phase::Delete,++deleted,static_cast<std::size_t>(deletions),d.relative);
  }
 }
 check_cancel(callbacks);
 auto l2=scan_entries(left,callbacks,Phase::ScanLeft);
 auto r2=scan_entries(right,callbacks,Phase::ScanRight);
 filter_exclusions(l2,options_.exclude_patterns);
 filter_exclusions(r2,options_.exclude_patterns);

 // There is deliberately no post-transfer checksum or integrity comparison.
 // Re-scan metadata only to persist the latest filesystem state.
 for(const auto& d:decisions){
  if(d.action==Action::Conflict||d.action==Action::Skip)continue;
  const auto li=l2.find(d.relative),ri=r2.find(d.relative);
  Pair p{li==l2.end()?Entry{}:li->second,ri==r2.end()?Entry{}:ri->second};
  if(!p.left.exists&&!p.right.exists)state.history.erase(d.relative);
  else state.history[d.relative]=p;
 }
 check_cancel(callbacks);
 emit(callbacks,Phase::Save,0,1);
 save(state_,state);
 events.record("committed",{}, "copied="+std::to_string(result.copied)+
  " deleted="+std::to_string(result.removed)+
  " archived="+std::to_string(result.archived)+
  " recycled="+std::to_string(result.recycled));
 if(!fs::remove(journal))throw std::runtime_error("Cannot finalize recovery journal");
 sync_parent(journal);
 emit(callbacks,Phase::Complete,1,1);
 }catch(const RunCancelled&){
  events.record("cancelled");
  throw;
 }catch(const std::exception& exception){
  try{events.record("error",{},exception.what());}catch(const std::exception&){}
  throw;
 }
 return result;
}
Result Engine::run(const fs::path& left,const fs::path& right,bool dry_run,const RunCallbacks& callbacks){
 LocalEndpoint l(left),r(right);return run(l,r,dry_run,callbacks);
}
}
