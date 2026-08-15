#pragma once

#include <cstdint>
#include <array>
#include <cstddef>

#ifndef UI_THEME_GEOMETRY_ONLY
#include <lvgl.h>
#else
struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;
#endif

namespace ui {

inline constexpr int kCanvasWidth = 400;
inline constexpr int kCanvasHeight = 300;
inline constexpr int kSafeMargin = 6;
inline constexpr int kSeparatorWidth = 1;
inline constexpr uint32_t kNavigationOverlayDurationMs = 2'000;
inline constexpr int kTileContentHeight = 64;
inline constexpr int kTileInset = 6;
inline constexpr int kTextStrokeWidth = 1;
inline constexpr int kTextInset = 1;
inline constexpr int kDataLineWidth = 2;

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  constexpr int right() const { return x + width; }
  constexpr int bottom() const { return y + height; }
};

struct TileTextLayout {
  Rect title;
  Rect value;
  Rect detail;
};

// Collapsed from the WMO forecast codes' dozen-plus condition strings (see
// weather_parse.cpp condition_for_wmo_code) down to the handful of shapes a
// 1-bit, backlight-less panel can actually resolve at a glance - see
// weather_icon_kind_for_condition in ui_data.hpp for the mapping and
// weather_icon below for the bold silhouette each one draws.
enum class WeatherIconKind { Sun, Cloud, Rain, Snow };

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

// General-purpose rect predicates used by static_assert layout proofs
// (ui_data.hpp) as well as host tests.
constexpr bool rect_within(const Rect outer, const Rect inner) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}

// Named rects_intersect (not rects_overlap) to avoid an ADL clash with the
// unrelated local helper of that name in tests/host/test_setup_page.cpp.
constexpr bool rects_intersect(const Rect a, const Rect b) {
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() &&
        b.y < a.bottom();
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

constexpr TileTextLayout tile_text_layout(const Rect cell) {
  const Rect content = tile_content_rect(cell);
  return {{cell.x + kTileInset, content.y, cell.width - 2 * kTileInset, 18},
          {cell.x + kTileInset, content.y + 18,
           cell.width - 2 * kTileInset, 28},
          {cell.x + kTileInset, content.y + 46,
           cell.width - 2 * kTileInset, 18}};
}

constexpr Rect tile_leading_visual_rect(const Rect cell,
                                        const bool visible) {
  const Rect value = tile_text_layout(cell).value;
  return visible ? Rect{cell.x + 8, value.y, 28, 28}
                 : Rect{value.x, value.y, 0, 0};
}

constexpr Rect tile_value_rect(const Rect cell, const bool with_leading_visual) {
  const Rect value = tile_text_layout(cell).value;
  return with_leading_visual
             ? Rect{cell.x + 43, value.y, cell.width - 49, value.height}
             : value;
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

constexpr int safe_text_box_height(const int requested_height,
                                   const int font_line_height) {
  const int stroke_safe_height = font_line_height + 2 * kTextInset;
  return requested_height > stroke_safe_height ? requested_height
                                                : stroke_safe_height;
}

constexpr int text_outline_width(const int font_line_height) {
  return font_line_height <= 16 ? kTextStrokeWidth : 0;
}

#ifndef UI_THEME_GEOMETRY_ONLY

void apply_surface(lv_obj_t* object);
lv_obj_t* label(lv_obj_t* parent, const char* text, Rect bounds,
                const lv_font_t* font = nullptr,
                lv_text_align_t align = LV_TEXT_ALIGN_LEFT);
lv_obj_t* divider(lv_obj_t* parent, Rect bounds);
lv_obj_t* line_segment(lv_obj_t* parent, int x, int y, int width, int height,
                       bool inverse = false);
void weather_icon(lv_obj_t* parent, Rect bounds, WeatherIconKind kind,
                  bool inverse = false);
void temperature_icon(lv_obj_t* parent, Rect bounds, bool inverse = false);
void humidity_icon(lv_obj_t* parent, Rect bounds, bool inverse = false);
void page_dots(lv_obj_t* parent, std::size_t page_index,
               std::size_t page_count, Rect bounds);
lv_obj_t* navigation_overlay(lv_obj_t* parent, Rect bounds);
// Inverts a label to a black-filled bar with white text when is_error is
// true, or restores the normal black-on-white look otherwise. Shared by the
// initial Setup page render and the label-only status repaint path so a
// status that flips from neutral to a failure (or back) while Setup stays
// on screen is restyled the same way a full page rebuild would style it.
void apply_setup_status_style(lv_obj_t* label_obj, bool is_error);

#endif

}  // namespace ui
