#pragma once

namespace synchrorep::winui {
struct UiRect {
 int x=0,y=0,w=0,h=0;
};
struct UiLayout {
 UiRect header,tabs,sidebar,content,summary,footer;
 bool showSummary=false;
 bool compact=false;
 int spacing=10;
};
// All coordinates are client pixels. The summary collapses before the form
// reaches an unusable width; the tab contents never require a vertical scrollbar.
UiLayout compute_layout(int width,int height);
bool fits_client(const UiLayout& layout,int width,int height);
// Compress row positions as the client approaches a 12-inch laptop's short
// available work area; the option rows must remain entirely inside the card.
int compact_form_y(const UiLayout& layout,int offset);
}
