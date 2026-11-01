#include "ui_layout.hpp"
#include <cassert>
#include <iostream>
#include <utility>

using namespace synchrorep::winui;
int main() {
 for(const auto& [w,h]:{
  std::pair{880,520},std::pair{1024,600},std::pair{1090,615},
  std::pair{1280,720},std::pair{1366,768},std::pair{1920,1080}}) {
  const auto layout=compute_layout(w,h);
  assert(fits_client(layout,w,h));
  assert(layout.content.w>=370);
  assert(layout.content.h>=330);
  assert(compact_form_y(layout,323)+24<=layout.content.h);
  assert(layout.showSummary==(w>=1100));
  assert(layout.sidebar.w<=216);
  assert(layout.footer.y>layout.tabs.y+layout.tabs.h);
 }
 const auto narrow=compute_layout(950,570);
 assert(!narrow.showSummary && narrow.content.w>=500);
 const auto wide=compute_layout(1280,720);
 assert(wide.showSummary && wide.summary.w>=208);
 std::cout<<"Responsive 12-inch layout tests passed\n";
}
