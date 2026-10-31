#include "synchrorep/mirror_monitor.hpp"
#include <chrono>
#include <map>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

namespace synchrorep {
namespace {
using Clock=std::chrono::steady_clock;
struct Stat {
 bool directory;
 std::uintmax_t size;
 fs::file_time_type modified;
 auto operator<=>(const Stat&)const=default;
};
using QuickState=std::map<fs::path,Stat>;
void verify_source(const fs::path& root){
 if(!fs::is_directory(root)||fs::is_symlink(fs::symlink_status(root)))
  throw std::runtime_error("Mirror local source is missing, not a directory, or a symbolic link");
#ifdef _WIN32
 const auto native=fs::absolute(root).native();
 if(native.size()<3 || native[1]!=L':' || (native[2]!=L'\\'&&native[2]!=L'/'))
  throw std::runtime_error("Mirror requires a local Windows drive, not UNC/WSL");
#endif
}
QuickState quick_state(const fs::path& root){
 verify_source(root);
 QuickState result;
 for(auto iterator=fs::recursive_directory_iterator(root);
          iterator!=fs::recursive_directory_iterator();++iterator){
  const auto st=iterator->symlink_status();
  if(fs::is_symlink(st)||(!fs::is_directory(st)&&!fs::is_regular_file(st)))
   throw std::runtime_error("Unsupported source object in mirror watcher");
  Stat value{fs::is_directory(st),0,{}};
  if(!value.directory){
   value.size=iterator->file_size();
   value.modified=iterator->last_write_time();
  }
  result.emplace(iterator->path().lexically_relative(root),value);
 }
 return result;
}
void pause(const RunCallbacks& callbacks,std::chrono::milliseconds length){
 constexpr auto quantum=std::chrono::milliseconds(50);
 for(auto elapsed=std::chrono::milliseconds(0);elapsed<length;elapsed+=quantum){
  if(callbacks.cancelled&&callbacks.cancelled())return;
  std::this_thread::sleep_for(std::min(quantum,length-elapsed));
 }
}
void accumulate(Result& total,const Result& run){
 total.copied+=run.copied;
 total.removed+=run.removed;
 total.archived+=run.archived;
 total.recycled+=run.recycled;
 total.conflicts.insert(total.conflicts.end(),run.conflicts.begin(),run.conflicts.end());
}
}
Result MirrorMonitor::run(Endpoint& source,Endpoint& target,Engine& engine,
                          const fs::path& local_directory,const RunCallbacks& callbacks)const{
 if(settings_.poll_interval.count()<100 || settings_.reconcile_interval.count()<1)
  throw std::invalid_argument("Invalid mirror monitor interval");
 if(!callbacks.cancelled)
  throw std::invalid_argument("Continuous mirror requires an explicit stop callback");
 if(!dynamic_cast<LocalEndpoint*>(&source))
  throw std::invalid_argument("Mirror monitor requires local source endpoint");
 verify_source(local_directory);
 Result total;
 // Initial full reconciliation, preserving the engine's journal/archive rules.
 auto once=engine.run(source,target,false,callbacks);
 if(!once.conflicts.empty())throw std::runtime_error("Mirror blocked by file/directory conflict");
 accumulate(total,once);
 auto baseline=quick_state(local_directory);
 auto last_full=Clock::now();
 // The source watcher is intentionally a periodic metadata check: no
 // platform-specific thread is launched and cancellation is bounded.
 while(!(callbacks.cancelled&&callbacks.cancelled())){
  if(callbacks.progress)callbacks.progress(Progress{Phase::Watch,0,0,{}});
  pause(callbacks,settings_.poll_interval);
  if(callbacks.cancelled&&callbacks.cancelled())break;
  auto current=quick_state(local_directory);
  auto now=Clock::now();
  // Confirm a changed tree is stable for one poll interval to debounce
  // editors that write in several steps or use atomic rename.
  if(current!=baseline){
   pause(callbacks,settings_.poll_interval);
   if(callbacks.cancelled&&callbacks.cancelled())break;
   auto after=quick_state(local_directory);
   if(current!=after)continue;
   // Keep the newest stable source snapshot; no reverse synchronization.
   current=std::move(after);
  }else if(now-last_full<settings_.reconcile_interval)continue;
  once=engine.run(source,target,false,callbacks);
  if(!once.conflicts.empty())throw std::runtime_error("Mirror blocked by file/directory conflict");
  accumulate(total,once);
  baseline=quick_state(local_directory);
  last_full=Clock::now();
 }
 return total;
}
}
