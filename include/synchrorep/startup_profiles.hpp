#pragma once
#include "synchrorep/profiles.hpp"
#include <string>
#include <vector>

namespace synchrorep {
struct StartupEntry {
 std::string value_name;   // deterministic registry value name, no credentials
 std::string profile_name; // persisted profile name; passed as one quoted argument
};
std::vector<StartupEntry> startup_entries(const std::vector<Profile>& profiles);
}
