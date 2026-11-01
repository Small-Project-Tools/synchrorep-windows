#pragma once
#include "synchrorep/endpoint.hpp"
#include <filesystem>
#include <map>
#include <string>
namespace synchrorep {
// SQLite representation of the original GNOME "files" history. This is an
// isolated storage component; the existing engine stays unchanged until
// migration and parity tests are complete.
struct StoredPair { Entry left; Entry right; };
using StoredHistory = std::map<std::string, StoredPair>;
class SqliteHistory {
public:
 explicit SqliteHistory(std::filesystem::path file);
 bool has_any_pair() const;
 void erase_pair(const std::string& left, const std::string& right) const;
 bool has_pair(const std::string& left, const std::string& right) const;
 StoredHistory load(const std::string& left, const std::string& right) const;
 void save(const std::string& left, const std::string& right,
           const StoredHistory& entries) const;
private:
 std::filesystem::path file_;
};
}
