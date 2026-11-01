// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/startup_profiles.hpp"
#include "synchrorep/sha256.hpp"
#include <stdexcept>
#include <set>
#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace synchrorep {
namespace {
std::string lower_ascii(std::string s){
 for(char& character:s)if(character>='A'&&character<='Z')
  character=static_cast<char>(character-'A'+'a');
 return s;
}
std::string normalized_path(std::string_view path,bool insensitive){
 std::string normalized;
 normalized.reserve(path.size());
 for(const char c:path)normalized+=c=='\\'?'/':c;
 std::vector<std::string> segments;
 std::size_t start=0;
 while(start<normalized.size()){
  auto end=normalized.find('/',start);
  if(end==std::string::npos)end=normalized.size();
  std::string segment=normalized.substr(start,end-start);
  if(insensitive)segment=lower_ascii(std::move(segment));
  if(segment==".."){
   if(segments.empty())throw std::invalid_argument("Destination path escapes its root");
   segments.pop_back();
  }else if(!segment.empty()&&segment!=".")segments.push_back(std::move(segment));
  start=end+1;
 }
 std::string result;
 for(const auto& segment:segments)result+="/"+segment;
 return result.empty()?"/":result;
}
}
std::string canonical_mirror_target(const std::string& location){
 if(location.empty())throw std::invalid_argument("Empty mirror target");
 const auto scheme=location.find("://");
 if(scheme!=std::string::npos){
  auto protocol=lower_ascii(location.substr(0,scheme));
  if(protocol!="ftp"&&protocol!="sftp"&&protocol!="wsl")
   throw std::invalid_argument("Unsupported mirror target scheme");
  const auto authority_start=scheme+3;
  auto slash=location.find('/',authority_start);
  auto authority=lower_ascii(location.substr(authority_start,
    slash==std::string::npos?std::string::npos:slash-authority_start));
  if(authority.empty()||authority.find('@')!=std::string::npos)
   throw std::invalid_argument("Invalid mirror target authority");
  if((protocol=="ftp"&&authority.ends_with(":21"))||
     (protocol=="sftp"&&authority.ends_with(":22")))
   authority.resize(authority.find_last_of(':'));
  const auto path=slash==std::string::npos?std::string_view("/"):
    std::string_view(location).substr(slash);
  if(path.find('%')!=std::string_view::npos)
   throw std::invalid_argument("Encoded mirror target paths cannot be compared safely");
  return protocol+"://"+authority+normalized_path(path,false);
 }
 std::string target=location;
 for(char& c:target)if(c=='\\')c='/';
 // Windows drive and UNC names are case-insensitive for our conservative
 // destination reservation; preserve case for POSIX paths.
 if(target.size()>=3&&std::isalpha(static_cast<unsigned char>(target[0]))&&
    target[1]==':'&&target[2]=='/'){
  const auto drive=lower_ascii(target.substr(0,2));
  return "file:"+drive+normalized_path(std::string_view(target).substr(2),true);
 }
 if(target.rfind("//?/",0)==0||target.rfind("//./",0)==0)
  throw std::invalid_argument("Extended Windows device paths are unsupported for mirrors");
 if(target.rfind("//",0)==0){
  // A WSL UNC path can refer to the same target as wsl://.
  const auto without_slashes=target.substr(2);
  const auto split=without_slashes.find('/');
  const auto host=lower_ascii(without_slashes.substr(0,split));
  if((host=="wsl.localhost"||host=="wsl$")&&split!=std::string::npos){
   auto rest=without_slashes.substr(split+1);
   const auto distro_end=rest.find('/');
   const auto distro=lower_ascii(rest.substr(0,distro_end));
   if(distro.empty())throw std::invalid_argument("Missing WSL distribution");
   const auto path=distro_end==std::string::npos?std::string("/"):rest.substr(distro_end);
   return "wsl://"+distro+normalized_path(path,false);
  }
  return "unc:"+normalized_path(std::string_view(target).substr(2),true);
 }
 if(target[0]=='/')return "file:"+normalized_path(target,false);
 // Do not register a relative destination for automatic startup.
 throw std::invalid_argument("Mirror target must be absolute");
}
bool mirror_targets_overlap(const std::string& first,const std::string& second){
 const auto a=canonical_mirror_target(first);
 const auto b=canonical_mirror_target(second);
 if(a==b)return true;
 auto below=[](const std::string& parent,const std::string& child){
  if(child.size()<=parent.size()||child.compare(0,parent.size(),parent)!=0)
   return false;
  return parent.back()=='/'||child[parent.size()]=='/';
 };
 return below(a,b)||below(b,a);
}
std::string mirror_target_mutex(const std::string& target){
 const auto canonical=canonical_mirror_target(target);
 Sha256 hash;
 hash.update(reinterpret_cast<const unsigned char*>(canonical.data()),canonical.size());
 return "Local\\SynchrorepMirror-"+hash.finish();
}
std::vector<StartupEntry> startup_entries(const std::vector<Profile>& profiles){
 std::vector<StartupEntry> entries;
 std::set<std::string> names,keys;
 std::vector<std::string> reserved_targets;
 for(const auto& profile:profiles){
  if(profile.name.empty()||profile.name.size()>500||
     profile.name.find_first_of("\x00\r\n",0,3)!=std::string::npos||
     !names.insert(profile.name).second)
   throw std::invalid_argument("Duplicate or empty profile name");
  if(!profile.auto_start)continue;
  if(profile.mode!=SyncMode::Mirror||profile.left.empty()||profile.right.empty())
   throw std::invalid_argument("Only complete mirror profiles may start with Windows");
  if(canonical_mirror_target(profile.left).rfind("file:",0)!=0)
   throw std::invalid_argument("Automatic mirror source must be an absolute local folder");
  (void)canonical_mirror_target(profile.right);
  for(const auto& reserved:reserved_targets)
   if(mirror_targets_overlap(profile.right,reserved))
    throw std::invalid_argument("Automatic mirrors cannot share or nest a destination");
  reserved_targets.push_back(profile.right);
  Sha256 hash;
  hash.update(reinterpret_cast<const unsigned char*>(profile.name.data()),profile.name.size());
  const auto key="SynchrorepMirror-"+hash.finish();
  if(!keys.insert(key).second)throw std::invalid_argument("Duplicate startup value");
  entries.push_back({key,profile.name});
 }
 return entries;
}
}
