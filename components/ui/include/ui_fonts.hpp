#pragma once

#include <lvgl.h>

namespace ui {

// The interface fonts. Each is Montserrat with the Traditional Chinese subset
// attached as an LVGL fallback, so a string mixing scripts - "Wi-Fi 設定" - draws
// from both without the caller choosing.
//
// Fallback rather than one merged face on purpose: every reserved row height in
// ui_data.hpp is derived from Montserrat's line height as a literal, and
// regenerating Montserrat through the subsetter would move those metrics under
// a layout that has been tuned against them. The generated CJK faces are
// shorter than their Montserrat counterparts at every size (14 vs 16, 19 vs 22,
// 27 vs 30), so Chinese cannot overflow a box sized for Latin.
//
// Call once, after lv_init and before the first render.
void fonts_init();

// Safe before fonts_init(), which is what makes these usable from static
// initialisers: until then each returns plain Montserrat and simply renders no
// Chinese, rather than dereferencing a half-built font.
const lv_font_t* font_small();   // Montserrat 14 + CJK 14
const lv_font_t* font_medium();  // Montserrat 20 + CJK 20
const lv_font_t* font_large();   // Montserrat 28 + CJK 28
// Clock hero: a 128px Montserrat subset of the ten digits and a colon, and
// nothing else. No CJK fallback, because no Chinese can reach it - and a
// Chinese subset at this size would cost more per glyph than every other size
// combined.
const lv_font_t* font_hero();

}  // namespace ui
