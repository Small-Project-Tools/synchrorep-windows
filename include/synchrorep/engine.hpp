#pragma once
#include "synchrorep/endpoint.hpp"
#include <vector>
#include <functional>
#include <map>
#include <stdexcept>
#include <utility>
namespace synchrorep {
// These decisions correspond to the original tc_files_binomial states.
enum class Action { CopyLeftToRight, CopyRightToLeft, DeleteLeft, DeleteRight, Conflict, None, Skip };
enum class ConflictPolicy { Ask, Ignore, Stop, Newest, Oldest };
enum class ConflictChoice { KeepLeft, KeepRight, Skip };
enum class Phase { ScanLeft, ScanRight, Copy, Delete, VerifyLeft, VerifyRight, Journal, Save, Complete };
struct Progress {
 Phase phase=Phase::ScanLeft;
 std::size_t completed=0;
 std::size_t total=0;
 std::string relative;
};
struct RunCallbacks {
 std::function<bool()> cancelled;
 std::function<void(const Progress&)> progress;
};
struct Options {
 ConflictPolicy conflict = ConflictPolicy::Ask;
 bool propagate_deletions = true;
 std::map<std::string,ConflictChoice> overrides;
 bool resume_interrupted = false;
 bool preserve_changes = true;
 Options(ConflictPolicy policy=ConflictPolicy::Ask,bool deletions=true,std::map<std::string,ConflictChoice> per_file={},bool resume=false,bool preserve=true)
 : conflict(policy),propagate_deletions(deletions),overrides(std::move(per_file)),
   resume_interrupted(resume),preserve_changes(preserve){}
};
struct Decision {std::string relative;Action action=Action::None;};
struct Result {std::size_t copied=0,removed=0,archived=0;std::vector<std::string> conflicts;};
class Engine {
public:
 explicit Engine(fs::path state,Options options={});
 bool has_pending_recovery()const;
 std::vector<Decision> plan(const Endpoint& left,const Endpoint& right,const RunCallbacks& callbacks={})const;
 std::vector<Decision> plan(const fs::path& left,const fs::path& right,const RunCallbacks& callbacks={})const;
 Result run(Endpoint& left,Endpoint& right,bool dry_run=false,const RunCallbacks& callbacks={});
 Result run(const fs::path& left,const fs::path& right,bool dry_run=false,const RunCallbacks& callbacks={});
private:
 fs::path state_;
 Options options_;
};
}
