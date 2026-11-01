#include "ui_layout.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace synchrorep::winui;

void require(bool condition, const std::string& message) {
 if (!condition) throw std::runtime_error(message);
}

int main() {
 try {
  for (const auto& [w,h] : {
   std::pair{880,520},std::pair{1024,600},std::pair{1090,615},
   std::pair{1280,720},std::pair{1366,768},std::pair{1920,1080}}) {
   const auto layout=compute_layout(w,h);
   const auto context=std::to_string(w)+"x"+std::to_string(h)+": ";
   require(fits_client(layout,w,h),context+"layout exceeds client");
   require(layout.content.w>=370,context+"content too narrow");
   require(layout.content.h>=330,context+"content too short");
   require(compact_form_y(layout,323)+24<=layout.content.h,
           context+"form overflows content");
   require(layout.showSummary==(w>=1100),context+"incorrect summary visibility");
   require(layout.sidebar.w<=216,context+"sidebar too wide");
   require(layout.footer.y>layout.tabs.y+layout.tabs.h,
           context+"footer overlaps navigation");
  }
  const auto narrow=compute_layout(950,570);
  require(!narrow.showSummary && narrow.content.w>=500,
          "950x570: narrow layout invalid");
  const auto wide=compute_layout(1280,720);
  require(wide.showSummary && wide.summary.w>=208,
          "1280x720: wide layout invalid");
  std::cout<<"Responsive 12-inch layout tests passed\n";
  return 0;
 } catch (const std::exception& error) {
  std::cerr<<"Responsive layout tests FAILED: "<<error.what()<<'\n';
  return 1;
 }
}
