// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "synchrorep/endpoint.hpp"
#include <filesystem>
#include <fstream>
#include <string>

namespace synchrorep {
// Per-run, append-only recovery archive stored outside synchronized roots.
// Archived content is verified against the SHA-256 snapshot before any mutation.
class RecoveryStore {
public:
 explicit RecoveryStore(fs::path directory);
 const fs::path& directory() const {return directory_;}
 // Archives an existing file (or records an empty directory) before mutation.
 // A successful call means the entry has been durably recorded in the manifest.
 void preserve(const Endpoint& endpoint,const std::string& relative,
               const Entry& expected,const std::string& reason);
private:
 fs::path directory_;
 fs::path content_root_;
 fs::path manifest_;
};
}
