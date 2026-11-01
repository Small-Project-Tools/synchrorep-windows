// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "synchrorep/profiles.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace synchrorep::winui {
// HKCU Run registration is per-user and never stores credentials.
// Enabled profiles are launched in separate processes, one watcher per profile.
void synchronize_startup(const std::vector<Profile>& profiles,const std::filesystem::path& executable,
                         const std::wstring& registry_path=
                           L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}
