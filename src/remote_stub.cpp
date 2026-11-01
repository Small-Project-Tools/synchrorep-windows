// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/remote_endpoint.hpp"
#include <stdexcept>
namespace synchrorep {
static void unsupported(){throw std::runtime_error("FTP/SFTP support is not included in this build (enable SYNCHROREP_REMOTE with libcurl SSH support)");}
RemoteEndpoint::RemoteEndpoint(std::string,RemoteCredentials){unsupported();}
std::string RemoteEndpoint::identity()const {unsupported();return {};}
void RemoteEndpoint::set_cancellation(std::function<bool()>)const {unsupported();}
Snapshot RemoteEndpoint::scan()const {unsupported();return {};}
std::unique_ptr<std::istream> RemoteEndpoint::read(const std::string&)const {unsupported();return {};}
void RemoteEndpoint::write(const std::string&,std::istream&,const Entry&){unsupported();}
void RemoteEndpoint::make_directory(const std::string&){unsupported();}
void RemoteEndpoint::remove(const std::string&){unsupported();}
std::string RemoteEndpoint::url_for(const std::string&,bool)const {unsupported();return {};}
std::string RemoteEndpoint::quote_path(const std::string&)const {unsupported();return {};}
void RemoteEndpoint::command(const std::vector<std::string>&,bool)const {unsupported();}
void RemoteEndpoint::scan_directory(const std::string&,Snapshot&,unsigned)const {unsupported();}
}
