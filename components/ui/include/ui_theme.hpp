#pragma once

#include <cstdint>

#ifndef UI_THEME_GEOMETRY_ONLY
#include <lvgl.h>
#endif

namespace ui {

inline constexpr int kCanvasWidth = 400;
inline constexpr int kCanvasHeight = 300;
inline constexpr int kSafeMargin = 6;
inline constexpr int kSeparatorWidth = 1;
inline constexpr uint32_t kNavigationOverlayDurationMs = 2'000;

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  constexpr int right() const { return x + width; }
  constexpr int bottom() const { return y + height; }
};

constexpr Rect safe_canvas() {
  return {kSafeMargin, kSafeMargin, kCanvasWidth - 2 * kSafeMargin,
          kCanvasHeight - 2 * kSafeMargin};
}

constexpr bool within_safe_canvas(const Rect rect) {
  const Rect canvas = safe_canvas();
  return rect.x >= canvas.x && rect.y >= canvas.y &&
         rect.right() <= canvas.right() && rect.bottom() <= canvas.bottom() &&
         rect.width >= 0 && rect.height >= 0;
}

#ifndef UI_THEME_GEOMETRY_ONLY

void apply_surface(lv_obj_t* object);
lv_obj_t* label(lv_obj_t* parent, const char* text, Rect bounds,
                const lv_font_t* font = nullptr,
                lv_text_align_t align = LV_TEXT_ALIGN_LEFT);
lv_obj_t* divider(lv_obj_t* parent, Rect bounds);
lv_obj_t* line_segment(lv_obj_t* parent, int x, int y, int width, int height,
                       bool inverse = false);
void weather_icon(lv_obj_t* parent, Rect bounds, bool rain,
                  bool inverse = false);
void temperature_icon(lv_obj_t* parent, Rect bounds, bool inverse = false);
void humidity_icon(lv_obj_t* parent, Rect bounds, bool inverse = false);
void page_dots(lv_obj_t* parent, uint8_t active_page, Rect bounds);
lv_obj_t* navigation_overlay(lv_obj_t* parent, Rect bounds);

#endif

}  // namespace ui
