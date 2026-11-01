// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <mutex>
#include <fstream>

namespace synchrorep {
// Machine-readable append-only log for troubleshooting and auditing.
class RunLog {
public:
 explicit RunLog(std::filesystem::path file);
 void record(const std::string& event,const std::string& path={},
             const std::string& detail={});
 const std::filesystem::path& path()const {return path_;}
private:
 std::filesystem::path path_;
 std::ofstream out_;
 std::mutex mutex_;
};
}
