#pragma once

#include <cstdint>
#include <array>

#ifndef UI_THEME_GEOMETRY_ONLY
#include <lvgl.h>
#endif

namespace ui {

inline constexpr int kCanvasWidth = 400;
inline constexpr int kCanvasHeight = 300;
inline constexpr int kSafeMargin = 6;
inline constexpr int kSeparatorWidth = 1;
inline constexpr uint32_t kNavigationOverlayDurationMs = 2'000;
inline constexpr int kTileContentHeight = 44;
inline constexpr int kTileInset = 6;

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

constexpr std::array<Rect, 3> right_tile_cells(const Rect bounds) {
  const int cell_height =
      (bounds.height - 2 * kSeparatorWidth) / 3;
  return {{{bounds.x, bounds.y, bounds.width, cell_height},
           {bounds.x, bounds.y + cell_height + kSeparatorWidth, bounds.width,
            cell_height},
           {bounds.x,
            bounds.y + 2 * (cell_height + kSeparatorWidth), bounds.width,
            bounds.height - 2 * (cell_height + kSeparatorWidth)}}};
}

constexpr Rect tile_content_rect(const Rect cell) {
  return {cell.x + kTileInset,
          cell.y + (cell.height - kTileContentHeight) / 2,
          cell.width - 2 * kTileInset, kTileContentHeight};
}

constexpr bool tile_content_is_centered(const Rect cell) {
  const Rect content = tile_content_rect(cell);
  const int cell_center = cell.y * 2 + cell.height;
  const int content_center = content.y * 2 + content.height;
  const int delta = content_center > cell_center
                        ? content_center - cell_center
                        : cell_center - content_center;
  return delta <= 2;
}

constexpr bool tile_content_has_no_reserved_footer(const Rect cell) {
  const Rect content = tile_content_rect(cell);
  return content.y >= cell.y && content.bottom() <= cell.bottom() &&
         cell.bottom() - content.bottom() < cell.height / 3;
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
