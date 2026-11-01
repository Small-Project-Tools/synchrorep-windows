#include "ui_layout.hpp"
#include <algorithm>

namespace synchrorep::winui {
UiLayout compute_layout(int width,int height) {
 width=std::max(1,width);
 height=std::max(1,height);
 UiLayout l{};
 const int pad=12;
 const int gap=10;
 const int head=48;
 const int nav=44;
 const int foot=58;
 l.header={0,0,width,head};
 l.tabs={0,head,width,nav};
 l.footer={pad,std::max(head+nav,height-foot),std::max(1,width-2*pad),foot-8};
 const int y=head+nav+pad;
 const int areaHeight=std::max(1,l.footer.y-y-gap);
 const int available=std::max(1,width-2*pad);
 l.compact=height<620;
 l.showSummary=width>=1100;
 const int left=std::clamp(width/5,175,216);
 const int right=l.showSummary?std::clamp(width/5,208,235):0;
 const int gaps=l.showSummary?2*gap:gap;
 const int middle=std::max(1,available-left-right-gaps);
 l.sidebar={pad,y,left,areaHeight};
 l.content={pad+left+gap,y,middle,areaHeight};
 l.summary=l.showSummary
  ?UiRect{l.content.x+middle+gap,y,right,areaHeight}:UiRect{};
 return l;
}
int compact_form_y(const UiLayout& l,int offset) {
 if(l.content.h>=360)return offset;
 const double scale=std::clamp((l.content.h-25)/335.0,0.80,1.0);
 return static_cast<int>(offset*scale);
}
bool fits_client(const UiLayout& l,int width,int height) {
 const UiRect rects[]={l.header,l.tabs,l.sidebar,l.content,l.footer};
 for(const auto& r:rects) {
  if(r.w<=0||r.h<=0||r.x<0||r.y<0||r.x+r.w>width||r.y+r.h>height)return false;
 }
 if(l.showSummary) {
  const auto& r=l.summary;
  if(r.w<=0||r.h<=0||r.x<0||r.y<0||r.x+r.w>width||r.y+r.h>height)return false;
 }
 return l.content.x>=l.sidebar.x+l.sidebar.w
   &&(!l.showSummary||l.summary.x>=l.content.x+l.content.w)
   &&l.footer.y>=l.content.y+l.content.h;
}
}
