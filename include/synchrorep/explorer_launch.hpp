#pragma once
#include "synchrorep/profiles.hpp"
#include <string>
#include <vector>

namespace synchrorep {
// Return a profile associated with a folder passed by Explorer, including
// both sides and Windows case/separator aliases. -1 means not configured.
int explorer_profile_index(const std::vector<Profile>& profiles,
                           const std::string& folder);

// Create a bidirectional profile with safe defaults. Both paths must be
// absolute native folders, distinct and non-nested; the caller checks
// existence on the local filesystem before invoking this function.
Profile explorer_new_profile(const std::vector<Profile>& profiles,
                             const std::string& source,
                             const std::string& destination);
}
