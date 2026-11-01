// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/sha256.hpp"
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <string>
#include <iostream>
using namespace synchrorep;
int main(){
 auto hash=[](const std::string& text) {
  std::istringstream s(text);return sha256(s,text.size());
 };
 assert(hash("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
 assert(hash("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
 assert(hash(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
 bool mismatch=false;
 try{std::istringstream s("abc");sha256(s,2);}catch(const std::runtime_error&){mismatch=true;}
 assert(mismatch);
 std::cout<<"SHA-256 known vectors and size check passed\n";
}
