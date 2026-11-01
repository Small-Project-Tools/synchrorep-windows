// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "synchrorep/endpoint.hpp"
#include <string>
#include <vector>

namespace synchrorep {
// Transport-specific implementation of the common synchronization endpoint.
// Credentials are passed at runtime, never embedded in URL or state files.
struct RemoteCredentials {
 std::string username;
 std::string password;
 std::string known_hosts; // Mandatory for SFTP, ignored by FTP.
 std::string private_key; // Optional for SFTP.
};
class RemoteEndpoint final : public Endpoint {
public:
 RemoteEndpoint(std::string url,RemoteCredentials credentials);
 std::string identity()const override;
 void set_cancellation(std::function<bool()> cancelled)const override;
 Snapshot scan()const override;
 std::unique_ptr<std::istream> read(const std::string& relative)const override;
 void write(const std::string& relative,std::istream& input,const Entry& source)override;
 void make_directory(const std::string& relative)override;
 void remove(const std::string& relative)override;
private:
 std::string root_;
 std::string scheme_;
 std::string remote_path_;
 RemoteCredentials credentials_;
 mutable Snapshot cached_;
 mutable std::function<bool()> cancelled_;
 std::string url_for(const std::string& relative,bool directory=false)const;
 std::string quote_path(const std::string& relative)const;
 void command(const std::vector<std::string>& commands,bool allow_cancel=true)const;
 void scan_directory(const std::string& relative,Snapshot& result,unsigned depth)const;
};
}
