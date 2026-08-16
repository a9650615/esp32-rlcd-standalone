#pragma once

// For InputHints, which button_hints below renders.
#include "settings_menu.hpp"
// For app_core::TrayIndicatorBitmap, which tray_indicator_icon() renders.
#include "tray_registry.hpp"

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
inline constexpr int kTileContentHeight = 76;  // 24 title + 34 value + 18 detail
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

// Title 24, value 34, detail 18. The title used to be the smallest row on the
// tile, which read as a caption under a number rather than a heading over one;
// at font 20 it is a heading. The value grows with it so the reading still
// dominates - equal sizes would remove the hierarchy rather than fix it.
inline constexpr int kTileTitleHeight = 24;
inline constexpr int kTileValueHeight = 34;
inline constexpr int kTileDetailHeight = 18;
constexpr TileTextLayout tile_text_layout(const Rect cell) {
  const Rect content = tile_content_rect(cell);
  return {{cell.x + kTileInset, content.y, cell.width - 2 * kTileInset,
           kTileTitleHeight},
          {cell.x + kTileInset, content.y + kTileTitleHeight,
           cell.width - 2 * kTileInset, kTileValueHeight},
          {cell.x + kTileInset,
           content.y + kTileTitleHeight + kTileValueHeight,
           cell.width - 2 * kTileInset, kTileDetailHeight}};
}

constexpr Rect tile_leading_visual_rect(const Rect cell,
                                        const bool visible) {
  const Rect value = tile_text_layout(cell).value;
  // 4px in from the cell edge, not 8. The sidebar cell is 108px wide and the
  // icon plus its old gap left only 57px for the value, which is 3px short of
  // "25.2 C" - measured on the device, not guessed.
  return visible ? Rect{cell.x + 4, value.y, 28, 28}
                 : Rect{value.x, value.y, 0, 0};
}

constexpr Rect tile_value_rect(const Rect cell, const bool with_leading_visual) {
  const Rect value = tile_text_layout(cell).value;
  return with_leading_visual
             ? Rect{cell.x + 36, value.y, cell.width - 40, value.height}
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

// Tray indicators, as plain data so UiContext can hold them in the LVGL-free
// host build. Both drawing calls return their mutable parts so a state change
// is applied in place: rebuilding the page to move a battery bar would repaint
// the whole reflective panel, which is what the cheap update path exists to
// avoid.
struct WifiIconParts {
  lv_obj_t* bars[3]{};
};

struct BatteryIconParts {
  lv_obj_t* fill = nullptr;
  int body_width = 0;
};

// A tray-registry indicator's icon: one LVGL canvas object showing exactly
// the bytes its module registered (see app_core::TrayIndicatorBitmap) -
// core never draws anything module-specific itself. Its cell is reserved
// unconditionally for every registered slot (see render_tray()) precisely
// so the cheap per-tick visibility toggle below always has a real target.
struct TrayIndicatorIcon {
  lv_obj_t* canvas = nullptr;
};

#ifndef UI_THEME_GEOMETRY_ONLY

void apply_surface(lv_obj_t* object);
// `wraps` only suppresses the debug overflow warning; it does not change the
// long mode. Prefer label_wrapped below, which does both.
lv_obj_t* label(lv_obj_t* parent, const char* text, Rect bounds,
                const lv_font_t* font = nullptr,
                lv_text_align_t align = LV_TEXT_ALIGN_LEFT,
                bool wraps = false);

// A label that wraps instead of ellipsising. One call rather than the
// create-then-set-long-mode pair, which had to be written four times and got
// the overflow warning wrong every time.
lv_obj_t* label_wrapped(lv_obj_t* parent, const char* text, Rect bounds,
                        const lv_font_t* font = nullptr,
                        lv_text_align_t align = LV_TEXT_ALIGN_LEFT);
lv_obj_t* divider(lv_obj_t* parent, Rect bounds);
lv_obj_t* line_segment(lv_obj_t* parent, int x, int y, int width, int height,
                       bool inverse = false);
void weather_icon(lv_obj_t* parent, Rect bounds, WeatherIconKind kind,
                  bool inverse = false);
void temperature_icon(lv_obj_t* parent, Rect bounds, bool inverse = false);
void humidity_icon(lv_obj_t* parent, Rect bounds, bool inverse = false);
// Fills the bottom band on pages where the buttons do something other than
// turn pages. A no-op when hints.visible is false.
WifiIconParts wifi_icon(lv_obj_t* parent, Rect bounds, bool connected);
void set_wifi_icon_state(const WifiIconParts& parts, bool connected);
BatteryIconParts battery_icon(lv_obj_t* parent, Rect bounds, uint8_t percent,
                              bool valid);
void set_battery_icon_level(const BatteryIconParts& parts, uint8_t percent,
                            bool valid);
// Renders a module's registered 1-bit bitmap as an LVGL canvas
// (LV_COLOR_FORMAT_I1) positioned at `bounds` - core blits the bytes and
// never interprets them. Returns an all-null TrayIndicatorIcon if `bitmap`
// is empty (nothing registered for this slot) or the canvas could not be
// created. See app_core::TrayIndicatorBitmap for the exact byte layout a
// module must supply.
TrayIndicatorIcon tray_indicator_icon(lv_obj_t* parent, Rect bounds,
                                      const app_core::TrayIndicatorBitmap& bitmap);
// Shows or hides the whole icon - not a per-part toggle like wifi_icon's
// rings, since a tray-registry icon is one opaque bitmap, not several
// hand-drawn primitives core understands individually.
void set_tray_indicator_icon_visible(const TrayIndicatorIcon& icon, bool visible);

void button_hints(lv_obj_t* parent, Rect bounds, InputHints hints);
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
