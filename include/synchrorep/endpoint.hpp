#pragma once
#include <cstdint>
#include <filesystem>
#include <istream>
#include <map>
#include <memory>
#include <string>

namespace synchrorep {
namespace fs = std::filesystem;

// The engine always works with relative POSIX-style names, regardless of the
// native path separator or the protocol used to reach an endpoint.
struct Entry {
 bool exists = false;
 bool directory = false;
 std::uintmax_t size = 0;
 std::int64_t modified = 0;
 std::string hash; // SHA-256 in hex; empty for directories or missing files.
};
using Snapshot = std::map<std::string, Entry>;

class Endpoint {
public:
 virtual ~Endpoint() = default;
 virtual std::string identity() const = 0;
 virtual Snapshot scan() const = 0;
 virtual std::unique_ptr<std::istream> read(const std::string& relative) const = 0;
 virtual void write(const std::string& relative, std::istream& input, const Entry& source) = 0;
 virtual void make_directory(const std::string& relative) = 0;
 virtual void remove(const std::string& relative) = 0;
};

class LocalEndpoint final : public Endpoint {
public:
 explicit LocalEndpoint(fs::path root);
 std::string identity() const override;
 Snapshot scan() const override;
 std::unique_ptr<std::istream> read(const std::string& relative) const override;
 void write(const std::string& relative, std::istream& input, const Entry& source) override;
 void make_directory(const std::string& relative) override;
 void remove(const std::string& relative) override;
 const fs::path& root() const {return root_;}
private:
 fs::path root_;
 fs::path within(const std::string& relative) const;
};

// Endpoint selection does not imply remote protocol support. Unsupported
// locations fail explicitly rather than falling back to a local path.
enum class EndpointKind { Local, Wsl, Ftp, Sftp };
EndpointKind endpoint_kind(const std::string& location);
fs::path wsl_unc_path(const std::string& uri);
std::unique_ptr<Endpoint> open_endpoint(const std::string& location);
}
