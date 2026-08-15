#include "ui_app.hpp"

#include <string>

#ifndef NDEBUG
#include <esp_log.h>
#endif

namespace ui {
namespace {

#ifndef NDEBUG
constexpr char kTag[] = "ui_geometry";
#endif

const lv_font_t* hero_font() { return &lv_font_montserrat_48; }
const lv_font_t* row_font() { return &lv_font_montserrat_20; }

}  // namespace

// Home keeps the Clock Hero plus one situational tile (see choose_home_tile/
// home_layout in ui_data.hpp) - the TODAY/NEXT/WEATHER-ALERT rows this page
// used to carry are gone, since that was the same "too much stuff, can't see
// what matters" clutter the one tile now covers on its own. Home has no
// system tray (page_shows_tray(Home) is false) and, unlike every tray page,
// keeps its page-dot indicator drawn directly here rather than through the
// tray - it is Home's only page-position cue.
void render_home(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds, std::size_t page_index,
                 std::size_t page_count, UiContext* context) {
#ifndef NDEBUG
  // Logged, not asserted: LVGL's assert handler is an infinite loop, so a
  // geometry complaint here would hang the LVGL task and blank the display
  // rather than tell anyone what was wrong. See render_shared.cpp.
  if (bounds.x < 0 || bounds.y < 0 || bounds.right() > kCanvasWidth ||
      bounds.bottom() > kCanvasHeight) {
    ESP_LOGW(kTag, "home bounds outside replacement root: x=%d y=%d w=%d h=%d",
             bounds.x, bounds.y, bounds.width, bounds.height);
  }
#endif
  apply_surface(parent);
  const HomeLayout layout = home_layout(bounds);
  const int split_x = layout.tile.x - kHomeSplitGap / 2;
  divider(parent, {split_x, bounds.y, kSeparatorWidth, layout.tile.height});

  const std::string clock = format_minute_clock(snapshot.clock.hero);
  lv_obj_t* clock_label =
      label(parent, clock.c_str(), layout.hero, hero_font(), LV_TEXT_ALIGN_LEFT);
  if (context != nullptr) context->staging_clock_label = clock_label;
  label(parent, snapshot.clock.date.c_str(), layout.date, row_font());
  // compact_clock_source already folds an empty/unrecognized source string
  // to "UNKNOWN", so it needs no separate emptiness check here.
  const std::string sync = "SOURCE  " + compact_clock_source(snapshot.clock.source);
  label(parent, sync.c_str(), layout.sync, row_font());

  render_right_tiles(parent, snapshot, layout.tile);
}

}  // namespace ui
