// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include <curl/curl.h>
#include <cassert>
#include <iostream>
#include <string_view>

int main(){
 const auto* version=curl_version_info(CURLVERSION_NOW);
 assert(version&&version->version&&version->protocols);
 assert((version->features&CURL_VERSION_SSL)!=0);
 bool ftp=false,sftp=false;
 for(auto item=version->protocols;*item;++item){
  const std::string_view protocol(*item);
  ftp|=protocol=="ftp";
  sftp|=protocol=="sftp";
 }
 if(!ftp||!sftp){
  std::cerr<<"Windows libcurl is missing FTP or SFTP support\n";
  return 1;
 }
 const std::string_view ssl(version->ssl_version?version->ssl_version:"");
 if(ssl.find("OpenSSL")==std::string_view::npos){
  std::cerr<<"Windows libcurl does not use the tested OpenSSL TLS/SSH backend\n";
  return 1;
 }
 std::cout<<"Windows libcurl "<<version->version
          <<" supports FTP and SFTP, TLS backend "<<ssl<<"\n";
}