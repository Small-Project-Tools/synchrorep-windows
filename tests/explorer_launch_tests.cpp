// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#include "synchrorep/explorer_launch.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace synchrorep;
template<class F> bool fails(F&& run){
 try{run();return false;}
 catch(const std::invalid_argument&){return true;}
}
int main(){
 const auto source=std::string("C:\\Users\\seb\\Documents\\Site tristan");
 const auto destination=std::string("D:\\Backup\\Site tristan");
 const auto created=explorer_new_profile({},source,destination);
 assert(created.left==source&&created.right==destination);
 assert(created.name=="Site tristan - Site tristan");
 assert(created.mode==SyncMode::Bidirectional);
 assert(created.conflict==ConflictPolicy::Ask);
 assert(created.propagate_deletions);
 assert(created.preserve_changes&&created.recycle_local_deletions);
 assert(!created.auto_start);
 assert(explorer_profile_index({created},"c:/users/SEB/Documents/Site tristan/")==0);
 assert(explorer_profile_index({created},"d:/BACKUP/site tristan")==0);
 assert(explorer_profile_index({created},"C:/Users/seb/no-match")==-1);
 auto alreadyNamed=created;
 alreadyNamed.name="Source - Destination";
 alreadyNamed.left="E:/Unrelated1";alreadyNamed.right="E:/Unrelated2";
 auto first=explorer_new_profile({alreadyNamed},"C:/Source","D:/Destination");
 assert(first.name=="Source - Destination (2)");
 auto name2=first;
 name2.name="Source - Destination (2)";
 name2.left="E:/Unrelated3";name2.right="E:/Unrelated4";
 auto second=explorer_new_profile({alreadyNamed,name2},"C:/Source","D:/Destination");
 assert(second.name=="Source - Destination (3)");
 assert(fails([&]{explorer_new_profile({created},source,"F:/Unrelated");}));
 assert(fails([&]{explorer_new_profile({created},"F:/Unrelated",destination);}));
 assert(fails([&]{explorer_new_profile({},source,source);}));
 assert(fails([&]{explorer_new_profile({},"C:/Parent","c:/parent/Child");}));
 assert(fails([&]{explorer_new_profile({},"C:/Parent/Child","c:/parent");}));
 assert(!fails([&]{explorer_new_profile({},"C:/Parent","C:/Parent2");}));
 assert(fails([&]{explorer_new_profile({},"relative","C:/Target");}));
 assert(fails([&]{explorer_new_profile({},"C:/Source","relative");}));
 assert(fails([&]{explorer_new_profile({},"ftp://host/foo","C:/Target");}));
 assert(fails([&]{explorer_new_profile({},"C:/Source","sftp://host/backup");}));
 assert(fails([&]{explorer_profile_index({created},"not-absolute");}));
 assert(explorer_profile_index({created},"C:/Users/seb/Documents/Site tristan/subdir")==-1);
 std::cout<<"Explorer launch flow profiles/defaults, duplicate paths and nesting passed\n";
}
