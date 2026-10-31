#include "synchrorep/startup_profiles.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace synchrorep;
bool fails(const auto& action){
 try{action();return false;}catch(const std::invalid_argument&){return true;}
}
int main(){
 Profile bidirectional;
 bidirectional.name="regular";
 bidirectional.left="C:/source";
 bidirectional.right="D:/target";
 Profile mirror=bidirectional;
 mirror.name="important mirror";
 mirror.mode=SyncMode::Mirror;
 mirror.auto_start=true;
 auto entries=startup_entries({bidirectional,mirror});
 assert(entries.size()==1);
 assert(entries.at(0).profile_name==mirror.name);
 assert(entries.at(0).value_name.rfind("SynchrorepMirror-",0)==0);
 auto same=startup_entries({mirror});
 assert(same.at(0).value_name==entries.at(0).value_name);
 mirror.name="another mirror";
 assert(startup_entries({mirror}).at(0).value_name!=entries.at(0).value_name);
 assert(startup_entries({bidirectional}).empty());
 bidirectional.auto_start=true;
 assert(fails([&]{startup_entries({bidirectional});}));
 mirror.mode=SyncMode::Mirror;
 mirror.left.clear();
 assert(fails([&]{startup_entries({mirror});}));
 mirror.left="C:/source";
 assert(canonical_mirror_target("C:\\DATA\\Archive\\")=="file:c:/data/archive");
 assert(canonical_mirror_target("c:/data/./archive")=="file:c:/data/archive");
 assert(canonical_mirror_target("sftp://HOST/backup/")=="sftp://host/backup");
 assert(canonical_mirror_target("wsl://Ubuntu/home/test")=="wsl://ubuntu/home/test");
 assert(mirror_targets_overlap("C:/data", "c:\\DATA\\child"));
 assert(mirror_targets_overlap("sftp://HOST/backup","sftp://host/backup/a"));
 assert(mirror_targets_overlap("wsl://Ubuntu/backup","wsl://ubuntu/backup/"));
 assert(mirror_targets_overlap("wsl://Ubuntu/backup","\\\\wsl.localhost\\Ubuntu\\backup\\nested"));
 assert(mirror_targets_overlap("sftp://host:22/root","sftp://host/root/child"));
 assert(mirror_targets_overlap("ftp://host:21/root","ftp://host/root"));
 assert(mirror_target_mutex("C:/DATA") == mirror_target_mutex("c:\\data\\"));
 assert(mirror_target_mutex("sftp://HOST:22/root")==mirror_target_mutex("sftp://host/root/"));
 assert(!mirror_targets_overlap("C:/data","C:/data2"));
 assert(!mirror_targets_overlap("sftp://host/a","sftp://other/a"));
 assert(!mirror_targets_overlap("sftp://host/a","sftp://host/a2"));
 assert(fails([&]{canonical_mirror_target("relative/folder");}));
 assert(fails([&]{canonical_mirror_target("\\\\?\\C:\\data");}));
 assert(fails([&]{canonical_mirror_target("sftp://host/a/%2e%2e/b");}));
 Profile second=mirror;
 second.name="second mirror";
 second.left="D:/other-source";
 second.right="sftp://same/root";
 mirror.right="sftp://same/root";
 assert(fails([&]{startup_entries({mirror,second});}));
 second.right="sftp://same/root/nested";
 assert(fails([&]{startup_entries({mirror,second});}));
 second.right="sftp://same/root2";
 assert(startup_entries({mirror,second}).size()==2);
 second.right="C:/safe";
 mirror.right="C:/safe-2";
 assert(startup_entries({mirror,second}).size()==2);
 mirror.right="C:/safe";
 assert(fails([&]{startup_entries({mirror,second});}));
 std::cout<<"Startup selection and stable registry keys passed\\n";
}
