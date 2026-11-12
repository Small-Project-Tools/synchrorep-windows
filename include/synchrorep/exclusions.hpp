// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace synchrorep::detail {
inline bool glob_matches(std::string_view pattern,std::string_view path){
 std::vector<unsigned char> memo((pattern.size()+1)*(path.size()+1),0);
 const auto width=path.size()+1;
 memo[pattern.size()*width+path.size()]=1;
 for(std::size_t i=pattern.size();i-->0;){
  for(std::size_t j=path.size()+1;j-->0;){
   const auto pos=i*width+j;
   if(pattern[i]=='*'){
    const bool recursive=i+1<pattern.size()&&pattern[i+1]=='*';
    const auto next=recursive?i+2:i+1;
    memo[pos]=memo[next*width+j]||
      (j<path.size()&&(recursive||path[j]!='/')&&memo[pos+1]);
    if(recursive&&next<pattern.size()&&pattern[next]=='/')
     memo[pos]|=memo[(next+1)*width+j];
   }else if(j<path.size()&&((pattern[i]=='?'&&path[j]!='/')||pattern[i]==path[j]))
    memo[pos]=memo[(i+1)*width+j+1];
  }
 }
 return memo[0]!=0;
}
inline bool excluded(std::string_view path,const std::vector<std::string>& patterns){
 for(const auto& pattern:patterns){
  if(pattern.empty())continue;
  if(pattern.size()>3&&pattern.ends_with("/**")&&
     path==std::string_view(pattern).substr(0,pattern.size()-3))return true;
  const bool basename=pattern.find('/')==std::string::npos;
  auto candidate=path;
  while(true){
   const auto last=candidate.find_last_of('/');
   const auto name=basename?candidate.substr(last==std::string_view::npos?0:last+1):candidate;
   if(glob_matches(pattern,name))return true;
   if(last==std::string_view::npos)break;
   candidate=candidate.substr(0,last);
  }
 }
 return false;
}
}
