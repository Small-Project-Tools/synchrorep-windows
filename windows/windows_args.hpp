// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
namespace synchrorep::winui {
// Quote a single Windows CRT argv argument, including trailing backslashes.
inline std::wstring quote_windows_argument(const std::wstring& input){
 std::wstring quoted=L"\"";
 std::size_t backslashes=0;
 for(const auto character:input){
  if(character==L'\\') {++backslashes;continue;}
  if(character==L'"'){
   quoted.append(backslashes*2+1,L'\\');
   quoted+=L'"';
   backslashes=0;
   continue;
  }
  quoted.append(backslashes,L'\\');
  backslashes=0;
  quoted+=character;
 }
 quoted.append(backslashes*2,L'\\');
 quoted+=L'"';
 return quoted;
}
}
