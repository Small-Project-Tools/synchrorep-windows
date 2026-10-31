#include "synchrorep/engine.hpp"
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
Snapshot scan_hashed(const Endpoint& endpoint,const RunCallbacks& callbacks,Phase phase){
 check_cancel(callbacks);
 auto entries=endpoint.scan();
 std::size_t done=0;
 emit(callbacks,phase,0,entries.size());
 for(auto& [name,info]:entries){
  check_cancel(callbacks);
  if(info.exists&&!info.directory){
   auto stream=endpoint.read(name);
   info.hash=sha256(*stream,info.size);
  }
  ++done;
  emit(callbacks,phase,done,entries.size(),name);
 }
 check_cancel(callbacks);
 return entries;
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
   return e.hash.size()==64&&
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
void save(const fs::path& file,const State& state) {
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
Action compare(const Entry& l,const Entry& r,const Pair& old,
               const Options& options){
 const bool cl=!same(l,old.left),cr=!same(r,old.right);
 if(l.exists&&r.exists&&l.directory!=r.directory)return Action::Conflict;
 if(!cl&&!cr)return Action::None;
 if(cl&&cr){
  if(!l.exists&&!r.exists)return Action::None;
  if(l.exists&&r.exists&&same(l,r))return Action::None;
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
  if(action==Action::Conflict) {
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
std::string unique_run_id(){
 static std::atomic<unsigned long long> serial{0};
 return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
   +"-"+std::to_string(++serial);
}
void ensure_external(const fs::path& archive,const Endpoint& left,const Endpoint& right){
 auto location=fs::weakly_canonical(archive);
 for(const auto* endpoint:{&left,&right}){
  auto local=dynamic_cast<const LocalEndpoint*>(endpoint);
  if(local && is_nested(fs::weakly_canonical(local->root()),location))
   throw std::runtime_error("Recovery archive must be outside synchronized roots");
 }
}
}
Engine::Engine(fs::path state,Options options):state_(std::move(state)),options_(options){}
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
 return build(scan_hashed(left,callbacks,Phase::ScanLeft),scan_hashed(right,callbacks,Phase::ScanRight),state.history,options_);
}
std::vector<Decision> Engine::plan(const fs::path& left,const fs::path& right,const RunCallbacks& callbacks)const{
 LocalEndpoint l(left),r(right);return plan(l,r,callbacks);
}
Result Engine::run(Endpoint& left,Endpoint& right,bool dry_run,const RunCallbacks& callbacks){
 ensure_distinct(left,right,state_);
 CancellationBinding binding(left,right,callbacks);
 auto state=load(state_,left,right);
 auto journal=journal_path(state_);
 if(!dry_run&&has_pending_recovery()){
  validate_journal(journal,left,right);
  if(!options_.resume_interrupted)
   throw std::runtime_error("Previous run interrupted; review the preview and explicitly enable recovery");
 }
 const auto leftScan=scan_hashed(left,callbacks,Phase::ScanLeft),rightScan=scan_hashed(right,callbacks,Phase::ScanRight);
 auto decisions=build(leftScan,rightScan,state.history,options_);
 Result result;
 for(const auto& decision:decisions)
  if(decision.action==Action::Conflict)result.conflicts.push_back(decision.relative);
 if(dry_run)return result;
 if(!result.conflicts.empty() && options_.conflict!=ConflictPolicy::Ignore)return result;
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
  slot->preserve(target,name,original,reason);
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
   erase(left,d.relative);++result.removed;
  }
  if(d.action==Action::DeleteRight){
   auto entry=rightScan.find(d.relative);
   if(entry==rightScan.end())throw std::runtime_error("Deletion target vanished: "+d.relative);
   preserve(right,d.relative,entry->second,"delete-right",false);
   events.record("delete-right",d.relative);
   erase(right,d.relative);++result.removed;
  }
  if(d.action==Action::DeleteRight||d.action==Action::DeleteLeft){
   journal_completed(journal,d.relative);
   emit(callbacks,Phase::Delete,++deleted,static_cast<std::size_t>(deletions),d.relative);
  }
 }
 check_cancel(callbacks);
 auto l2=scan_hashed(left,callbacks,Phase::VerifyLeft),r2=scan_hashed(right,callbacks,Phase::VerifyRight);
 // Never learn a state if a file changed during copying or its destination
 // did not match the source. This also catches partial remote replacements.
 for(const auto& d:decisions){
  if(d.action==Action::CopyLeftToRight||d.action==Action::CopyRightToLeft){
   const auto& before=d.action==Action::CopyLeftToRight?leftScan:rightScan;
   const auto& after=d.action==Action::CopyLeftToRight?l2:r2;
   const auto& dest=d.action==Action::CopyLeftToRight?r2:l2;
   if(!after.count(d.relative)||!dest.count(d.relative)||
      !same(before.at(d.relative),after.at(d.relative))||
      !same(after.at(d.relative),dest.at(d.relative)))
    throw std::runtime_error("Source changed during transfer or destination content mismatch: "+d.relative);
  }
 }
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
  " archived="+std::to_string(result.archived));
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
