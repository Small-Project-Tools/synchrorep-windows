// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "synchrorep/profiles.hpp"
#include <string>
#include <vector>

namespace synchrorep {
struct StartupEntry {
 std::string value_name;   // deterministic registry value name, no credentials
 std::string profile_name; // persisted profile name; passed as one quoted argument
};
// Canonical destination identity used for exclusive per-target mirror locks.
std::string canonical_mirror_target(const std::string& location);
// True if a destination contains the other destination (same or nested paths).
bool mirror_targets_overlap(const std::string& first,const std::string& second);
// Hashed lock identifier shared by all Windows processes using this target.
std::string mirror_target_mutex(const std::string& target);
std::vector<StartupEntry> startup_entries(const std::vector<Profile>& profiles);
}
