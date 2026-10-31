#pragma once
#include "synchrorep/endpoint.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace synchrorep {
struct RecoveryItem {
 fs::path archive_side; // path to a single left/right archive, contains manifest.tsv and data/
 std::string relative;
 std::string endpoint;
 std::string reason;
 std::string side;
 std::string hash;
 std::uintmax_t size=0;
 bool directory=false;
};
class RecoveryCatalog {
public:
 // Recursively enumerates the expected recovery/state/run/side layout.
 static std::vector<RecoveryItem> list(const fs::path& recovery_root);
 // Creates a local export, NEVER replaces an existing target.
 // Rejects symlinks, path traversal, missing content, or digest mismatches.
 static fs::path export_item(const RecoveryItem& item,const fs::path& destination_root);
};
}
