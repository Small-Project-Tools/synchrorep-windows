#pragma once
#include "synchrorep/endpoint.hpp"
#include <vector>
namespace synchrorep {
// These decisions correspond to the original tc_files_binomial states.
enum class Action { CopyLeftToRight, CopyRightToLeft, DeleteLeft, DeleteRight, Conflict, None };
enum class ConflictPolicy { Ask, Ignore, Stop, Newest, Oldest };
struct Options {
 ConflictPolicy conflict = ConflictPolicy::Ask;
 bool propagate_deletions = true;
};
struct Decision {std::string relative;Action action=Action::None;};
struct Result {std::size_t copied=0,removed=0;std::vector<std::string> conflicts;};
class Engine {
public:
 explicit Engine(fs::path state,Options options={});
 std::vector<Decision> plan(const Endpoint& left,const Endpoint& right)const;
 std::vector<Decision> plan(const fs::path& left,const fs::path& right)const;
 Result run(Endpoint& left,Endpoint& right,bool dry_run=false);
 Result run(const fs::path& left,const fs::path& right,bool dry_run=false);
private:
 fs::path state_;
 Options options_;
};
}
