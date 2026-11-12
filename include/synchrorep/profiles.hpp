// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
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
 bool preserve_changes=true;
 bool recycle_local_deletions=true;
 SyncMode mode=SyncMode::Bidirectional;
 bool auto_start=false;
 bool verify_hash=false;
 bool resume_interrupted=true;
 std::vector<std::string> exclude_patterns;
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
