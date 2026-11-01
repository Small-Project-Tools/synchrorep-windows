#include "ui_theme.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <string>
#include <algorithm>

namespace synchrorep::winui {
namespace {
HFONT body=nullptr;
HFONT heading=nullptr;
HBRUSH cardBrush=nullptr;
HBRUSH inputBrush=nullptr;
void rectangle(HDC dc,RECT r,COLORREF fill,COLORREF outline,int radius=12) {
 const auto b=CreateSolidBrush(fill);
 const auto p=CreatePen(PS_SOLID,1,outline);
 const auto oldB=SelectObject(dc,b);
 const auto oldP=SelectObject(dc,p);
 RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
 SelectObject(dc,oldP);
 SelectObject(dc,oldB);
 DeleteObject(p);
 DeleteObject(b);
}
RECT rect(const UiRect& r){return {r.x,r.y,r.x+r.w,r.y+r.h};}
void headline(HDC dc,int x,int y,const wchar_t* text,COLORREF color=color::white) {
 const auto old=SelectObject(dc,ui_heading_font());
 SetBkMode(dc,TRANSPARENT);
 SetTextColor(dc,color);
 RECT area{x,y,x+580,y+27};
 DrawTextW(dc,text,-1,&area,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
 SelectObject(dc,old);
}
void smallText(HDC dc,int x,int y,const wchar_t* text,COLORREF color=color::muted) {
 const auto old=SelectObject(dc,ui_font());
 SetBkMode(dc,TRANSPARENT);
 SetTextColor(dc,color);
 RECT area{x,y,x+580,y+22};
 DrawTextW(dc,text,-1,&area,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
 SelectObject(dc,old);
}
LRESULT CALLBACK checkboxProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,
                                UINT_PTR,DWORD_PTR) {
 switch(message) {
 case WM_PAINT:{
  PAINTSTRUCT ps{};
  HDC dc=BeginPaint(hwnd,&ps);
  RECT area{};GetClientRect(hwnd,&area);
  FillRect(dc,&area,ui_card_brush());
  const int y=(area.bottom-area.top-17)/2;
  const bool enabled=IsWindowEnabled(hwnd)!=FALSE;
  const bool checked=SendMessageW(hwnd,BM_GETCHECK,0,0)==BST_CHECKED;
  rectangle(dc,RECT{2,y,19,y+17},checked?color::blue:color::input,
            checked?color::blue:color::border,5);
  if(checked) {
   SetTextColor(dc,color::white);
   SetBkMode(dc,TRANSPARENT);
   HPEN pen=CreatePen(PS_SOLID,2,color::white);
   const auto old=SelectObject(dc,pen);
   MoveToEx(dc,5,y+9,nullptr);
   LineTo(dc,9,y+13);
   LineTo(dc,16,y+4);
   SelectObject(dc,old);
   DeleteObject(pen);
  }
  wchar_t name[256]{};
  GetWindowTextW(hwnd,name,256);
  SetTextColor(dc,enabled?color::white:color::muted);
  SetBkMode(dc,TRANSPARENT);
  RECT text{27,0,area.right-2,area.bottom};
  const auto old=SelectObject(dc,ui_font());
  DrawTextW(dc,name,-1,&text,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
  SelectObject(dc,old);
  EndPaint(hwnd,&ps);
  return 0;
 }
 case WM_ERASEBKGND:return 1;
 case WM_ENABLE:
 case WM_LBUTTONUP:{
  const auto result=DefSubclassProc(hwnd,message,wp,lp);
  InvalidateRect(hwnd,nullptr,FALSE);
  return result;
 }
 case WM_NCDESTROY:
  RemoveWindowSubclass(hwnd,checkboxProc,1);break;
 default:break;
 }
 return DefSubclassProc(hwnd,message,wp,lp);
}
LRESULT CALLBACK buttonProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,
                              UINT_PTR,DWORD_PTR) {
 switch(message) {
 case WM_MOUSEMOVE:
  if(GetWindowLongPtrW(hwnd,GWLP_USERDATA)==0) {
   SetWindowLongPtrW(hwnd,GWLP_USERDATA,1);
   TRACKMOUSEEVENT event{sizeof(event),TME_LEAVE,hwnd,0};
   TrackMouseEvent(&event);
   InvalidateRect(hwnd,nullptr,FALSE);
  }
  break;
 case WM_MOUSELEAVE:
  SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
  InvalidateRect(hwnd,nullptr,FALSE);
  break;
 case WM_NCDESTROY:
  RemoveWindowSubclass(hwnd,buttonProc,2);break;
 default:break;
 }
 return DefSubclassProc(hwnd,message,wp,lp);
}
}

HFONT ui_font() {
 if(!body)body=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
  DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
  DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
 return body;
}
HFONT ui_heading_font() {
 if(!heading)heading=CreateFontW(-17,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,
  DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
  DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
 return heading;
}
HBRUSH ui_card_brush() {
 if(!cardBrush)cardBrush=CreateSolidBrush(color::card);
 return cardBrush;
}
HBRUSH ui_input_brush() {
 if(!inputBrush)inputBrush=CreateSolidBrush(color::input);
 return inputBrush;
}
void ui_paint_frame(HDC dc,const UiLayout& l,const wchar_t* section,bool busy,double progress) {
 RECT canvas{0,0,l.header.w,l.footer.y+l.footer.h+8};
 const auto background=CreateSolidBrush(color::background);
 FillRect(dc,&canvas,background);
 DeleteObject(background);
 RECT title{0,0,l.header.w,l.header.h};
 const auto top=CreateSolidBrush(color::header);
 FillRect(dc,&title,top);
 DeleteObject(top);
 const auto icon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));
 if(icon)DrawIconEx(dc,20,10,icon,27,27,0,nullptr,DI_NORMAL);
 headline(dc,56,12,L"Synchrorep");
 smallText(dc,204,14,L"File synchronization");
 rectangle(dc,RECT{l.header.w-116,12,l.header.w-21,38},
           color::card,color::border,18);
 smallText(dc,l.header.w-94,14,busy?L"Running":L"Ready",
           busy?color::blue:color::green);
 RECT tabBand{0,l.tabs.y,l.tabs.w,l.tabs.y+l.tabs.h};
 const auto band=CreateSolidBrush(color::header);
 FillRect(dc,&tabBand,band);
 DeleteObject(band);
 const auto divider=CreatePen(PS_SOLID,1,color::border);
 const auto oldPen=SelectObject(dc,divider);
 MoveToEx(dc,0,l.tabs.y+l.tabs.h-1,nullptr);
 LineTo(dc,l.tabs.w,l.tabs.y+l.tabs.h-1);
 SelectObject(dc,oldPen);
 DeleteObject(divider);
 for(const auto& r:{l.sidebar,l.content})rectangle(dc,rect(r),color::card,color::border);
 if(l.showSummary)rectangle(dc,rect(l.summary),color::card,color::border);
 rectangle(dc,rect(l.footer),color::header,color::border);
 if(busy) {
  const int width=std::max(100,l.footer.w-495);
  const int x=l.footer.x+14;
  const int y=l.footer.y+l.footer.h-11;
  rectangle(dc,RECT{x,y,x+width,y+5},color::border,color::border,5);
  const int filled=std::clamp(static_cast<int>(width*progress),12,width);
  rectangle(dc,RECT{x,y,x+filled,y+5},color::blue,color::blue,5);
 }
 headline(dc,l.sidebar.x+14,l.sidebar.y+13,L"Sync profiles");
 headline(dc,l.content.x+16,l.content.y+13,section);
 if(l.showSummary) {
  headline(dc,l.summary.x+16,l.summary.y+13,L"Live summary");
  const auto line=CreatePen(PS_SOLID,1,color::border);
  const auto old=SelectObject(dc,line);
  MoveToEx(dc,l.summary.x+14,l.summary.y+100,nullptr);
  LineTo(dc,l.summary.x+l.summary.w-14,l.summary.y+100);
  SelectObject(dc,old);
  DeleteObject(line);
 }
}
void ui_paint_edit_frame(HDC dc,RECT bounds) {
 rectangle(dc,bounds,color::input,color::border,8);
}
void ui_draw_button(const DRAWITEMSTRUCT& item,bool selected,bool primary,bool dangerous) {
 HDC dc=item.hDC;
 RECT bounds=item.rcItem;
 const bool enabled=(item.itemState&ODS_DISABLED)==0;
 const bool pressed=(item.itemState&ODS_SELECTED)!=0;
 const bool hovered=GetWindowLongPtrW(item.hwndItem,GWLP_USERDATA)!=0;
 COLORREF fill=selected||primary?color::blue:color::input;
 COLORREF line=selected||primary?color::blue:color::border;
 if(!enabled)fill=color::card;
 else if(pressed)fill=color::hover;
 else if(hovered&&!primary&&!selected)fill=color::hover;
 else if(hovered&&(primary||selected))fill=RGB(59,143,255);
 if(dangerous&&hovered)fill=color::red;
 rectangle(dc,bounds,fill,line,10);
 wchar_t text[256]{};
 GetWindowTextW(item.hwndItem,text,256);
 SetBkMode(dc,TRANSPARENT);
 SetTextColor(dc,enabled?color::white:color::muted);
 const auto old=SelectObject(dc,ui_font());
 RECT label=bounds;
 label.left+=7;label.right-=7;
 DrawTextW(dc,text,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
 SelectObject(dc,old);
 if(item.itemState&ODS_FOCUS) {
  RECT focus=bounds;InflateRect(&focus,-5,-4);
  DrawFocusRect(dc,&focus);
 }
}
void ui_draw_profile(const DRAWITEMSTRUCT& item,const wchar_t* name) {
 if(item.itemID==static_cast<UINT>(-1))return;
 const bool selected=(item.itemState&ODS_SELECTED)!=0;
 RECT r=item.rcItem;
 FillRect(item.hDC,&r,ui_card_brush());
 r.left+=6;r.right-=6;r.top+=3;r.bottom-=3;
 rectangle(item.hDC,r,selected?RGB(24,57,94):color::input,
           selected?color::blue:color::border,10);
 SetBkMode(item.hDC,TRANSPARENT);
 SetTextColor(item.hDC,color::white);
 RECT text{r.left+12,r.top+2,r.right-8,r.bottom-3};
 const auto old=SelectObject(item.hDC,ui_heading_font());
 DrawTextW(item.hDC,name,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
 SelectObject(item.hDC,old);
}
void ui_style_checkbox(HWND hwnd) {
 SetWindowSubclass(hwnd,checkboxProc,1,0);
}
void ui_style_button(HWND hwnd) {
 SetWindowSubclass(hwnd,buttonProc,2,0);
}
void ui_set_dark_title(HWND hwnd) {
 const BOOL enabled=TRUE;
 DwmSetWindowAttribute(hwnd,20,&enabled,sizeof(enabled));
}
void ui_cleanup() {
 if(body){DeleteObject(body);body=nullptr;}
 if(heading){DeleteObject(heading);heading=nullptr;}
 if(cardBrush){DeleteObject(cardBrush);cardBrush=nullptr;}
 if(inputBrush){DeleteObject(inputBrush);inputBrush=nullptr;}
}
}
