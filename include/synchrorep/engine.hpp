#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace synchrorep {
namespace fs=std::filesystem;
enum class Action { CopyLeftToRight, CopyRightToLeft, DeleteLeft, DeleteRight, Conflict, None };
struct Entry { bool exists=false; bool directory=false; std::uintmax_t size=0; std::int64_t modified=0; };
struct Decision { std::string relative; Action action=Action::None; };
struct Result { std::size_t copied=0, removed=0; std::vector<std::string> conflicts; };
class Engine {
public:
 explicit Engine(fs::path state);
 std::vector<Decision> plan(const fs::path& left, const fs::path& right) const;
 Result run(const fs::path& left,const fs::path& right,bool dry_run=false);
private:
 fs::path state_;
};
}
