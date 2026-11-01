// Copyright (C) 2026 Sébastien Kus
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui_layout.hpp"

namespace synchrorep::winui {
namespace color {
 constexpr COLORREF background=RGB(10,17,29);
 constexpr COLORREF header=RGB(13,23,39);
 constexpr COLORREF card=RGB(21,32,50);
 constexpr COLORREF input=RGB(14,24,40);
 constexpr COLORREF border=RGB(43,62,88);
 constexpr COLORREF white=RGB(237,243,252);
 constexpr COLORREF muted=RGB(163,179,203);
 constexpr COLORREF blue=RGB(45,125,243);
 constexpr COLORREF hover=RGB(36,54,80);
 constexpr COLORREF green=RGB(63,202,127);
 constexpr COLORREF red=RGB(238,104,106);
}
HFONT ui_font();
HFONT ui_heading_font();
HBRUSH ui_card_brush();
HBRUSH ui_input_brush();
void ui_paint_frame(HDC dc,const UiLayout& layout,const wchar_t* section,bool busy,double progress);
void ui_paint_edit_frame(HDC dc,RECT bounds);
void ui_draw_button(const DRAWITEMSTRUCT& item,bool selected,bool primary,bool dangerous);
void ui_draw_profile(const DRAWITEMSTRUCT& item,const wchar_t* name);
void ui_style_checkbox(HWND hwnd);
void ui_style_button(HWND hwnd);
void ui_set_dark_title(HWND hwnd);
void ui_cleanup();
}
