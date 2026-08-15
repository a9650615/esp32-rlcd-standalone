#include "ui_app.hpp"

#include <cstdio>
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
const lv_font_t* medium_font() { return &lv_font_montserrat_20; }
const lv_font_t* small_font() { return &lv_font_montserrat_14; }

std::string sync_status(const app_core::AppSnapshot& snapshot) {
  const std::string source = snapshot.clock.source.empty()
                                 ? "SOURCE UNKNOWN"
                                 : snapshot.clock.source;
  return "SYNCED  " + source + "  AGE --";
}

std::string next_event(const app_core::AppSnapshot& snapshot) {
  const std::string market = snapshot.taiwan_market.display_name.empty()
                                 ? "MARKET"
                                 : snapshot.taiwan_market.display_name;
  const std::string index = snapshot.taiwan_market.primary_label.empty()
                                ? "INDEX"
                                : snapshot.taiwan_market.primary_label;
  return "NEXT  " + market + "  " + index;
}

}  // namespace

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
  const int right_width = 118;
  const int split_gap = 12;
  const Rect right{bounds.right() - right_width, bounds.y, right_width,
                   bounds.height - 12};
  const Rect left{bounds.x, bounds.y, bounds.width - right_width - split_gap,
                  bounds.height - 12};
  const int split_x = right.x - split_gap / 2;
  divider(parent, {split_x, bounds.y, kSeparatorWidth, left.height});

  const std::string clock = format_minute_clock(snapshot.clock.hero);
  lv_obj_t* clock_label =
      label(parent, clock.c_str(), {left.x + 2, left.y + 2, left.width - 4, 59},
            hero_font(), LV_TEXT_ALIGN_LEFT);
  if (context != nullptr) context->staging_clock_label = clock_label;
  label(parent, snapshot.clock.date.c_str(),
        {left.x + 3, left.y + 64, left.width - 6, 22}, small_font());
  const std::string sync = sync_status(snapshot);
  label(parent, sync.c_str(),
        {left.x + 3, left.y + 87, left.width - 6, 18}, small_font());
  divider(parent, {left.x, left.y + 111, left.width, kSeparatorWidth});

  label(parent, "TODAY", {left.x + 3, left.y + 121, 52, 18}, small_font());
  label(parent, snapshot.taiwan_market.display_name.c_str(),
        {left.x + 65, left.y + 121, left.width - 6 - 65, 18}, small_font());
  const std::string event = next_event(snapshot);
  label(parent, event.c_str(), {left.x + 3, left.y + 146, left.width - 6, 23},
        medium_font());
  char weather_status[56];
  std::snprintf(weather_status, sizeof(weather_status), "%s %u%%",
                snapshot.weather.alert ? "WEATHER ALERT" : "WEATHER",
                snapshot.weather.current.rain_probability_percent);
  label(parent, weather_status,
        {left.x + 3, left.y + 178, left.width - 6, 19}, small_font());
  weather_icon(parent, {left.x + 3, left.y + 207, 31, 29},
               snapshot.weather.current.condition == "Rain");
  char weather_line[56];
  std::snprintf(weather_line, sizeof(weather_line), "%s  %.0f C   HIGH %.0f",
                snapshot.weather.current.location.c_str(),
                snapshot.weather.current.temperature_c,
                snapshot.weather.seven_day[0].high_c);
  label(parent, weather_line, {left.x + 40, left.y + 209, left.width - 43, 22},
        small_font());
  label(parent, "REFLECTIVE MONO  /  SNAPSHOT ONLY",
        {left.x + 3, left.y + 243, left.width - 6, 18}, small_font());

  render_right_tiles(parent, snapshot, right);
  page_dots(parent, page_index, page_count, bounds);
}

}  // namespace ui
