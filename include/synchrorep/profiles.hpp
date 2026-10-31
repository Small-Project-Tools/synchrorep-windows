#pragma once
#include "synchrorep/engine.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace synchrorep {
struct Profile {
 std::string name;
 std::string left;
 std::string right;
 std::string known_hosts;
 ConflictPolicy conflict=ConflictPolicy::Ask;
 bool propagate_deletions=true;
};
class ProfileStore {
public:
 explicit ProfileStore(fs::path file):file_(std::move(file)){}
 std::vector<Profile> load() const;
 void save(const std::vector<Profile>& profiles) const;
private:
 fs::path file_;
};
}
