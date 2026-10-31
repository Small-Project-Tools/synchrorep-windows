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
 assert(fails([&]{startup_entries({mirror,mirror});}));
 std::cout<<"Startup selection and stable registry keys passed\\n";
}
