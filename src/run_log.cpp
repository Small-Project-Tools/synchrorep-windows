// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/run_log.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace synchrorep {
namespace {
std::string escape(const std::string& text) {
 std::string out;
 for(unsigned char c:text){
  switch(c){
  case '\\':out+="\\\\";break;
  case '"':out+="\\\"";break;
  case '\n':out+="\\n";break;
  case '\r':out+="\\r";break;
  case '\t':out+="\\t";break;
  default:
   if(c<0x20){
    const char* hex="0123456789abcdef";
    out+="\\u00";out+=hex[c>>4];out+=hex[c&15];
   }else out+=static_cast<char>(c);
  }
 }
 return out;
}
}
RunLog::RunLog(std::filesystem::path file):path_(std::move(file)){
 auto parent=path_.parent_path();
 if(!parent.empty())std::filesystem::create_directories(parent);
 out_.open(path_,std::ios::binary|std::ios::app);
 if(!out_)throw std::runtime_error("Cannot open execution log");
}
void RunLog::record(const std::string& event,const std::string& path,
                    const std::string& detail) {
 std::lock_guard lock(mutex_);
 const auto now=std::chrono::system_clock::now();
 const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
 out_<<"{\"unix_ms\":"<<ms<<",\"event\":\""<<escape(event)
     <<"\",\"path\":\""<<escape(path)<<"\",\"detail\":\""
     <<escape(detail)<<"\"}\n";
 out_.flush();
 if(!out_)throw std::runtime_error("Cannot flush execution log");
}
}
