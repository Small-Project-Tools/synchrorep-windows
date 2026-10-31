#pragma once
#include <filesystem>
namespace synchrorep {
// These functions return false on Linux and for UNC/WSL/network paths.
// Recycle is deliberately strict: there is no permanent-delete fallback.
bool supports_native_recycle(const std::filesystem::path& folder) noexcept;
void move_to_native_recycle(const std::filesystem::path& absolute_path);
}
