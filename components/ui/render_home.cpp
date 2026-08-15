#include "ui_app.hpp"

#include <cstdio>
#include <string>

namespace ui {
namespace {

const lv_font_t* hero_font() { return &lv_font_montserrat_48; }
const lv_font_t* medium_font() { return &lv_font_montserrat_20; }
const lv_font_t* small_font() { return &lv_font_montserrat_14; }

std::string minute_clock(std::string clock) {
  const std::size_t first = clock.find(':');
  if (first != std::string::npos) {
    const std::size_t second = clock.find(':', first + 1);
    if (second != std::string::npos) clock.resize(second);
  }
  return clock;
}

}  // namespace

void render_home(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds, uint8_t active_page) {
#ifndef NDEBUG
  LV_ASSERT_MSG(bounds.x >= 0 && bounds.y >= 0 &&
                    bounds.right() <= kCanvasWidth &&
                    bounds.bottom() <= kCanvasHeight,
                "home bounds outside replacement root");
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

  const std::string clock = minute_clock(snapshot.clock.hero);
  label(parent, clock.c_str(), {left.x + 2, left.y + 2, left.width - 4, 59},
        hero_font(), LV_TEXT_ALIGN_LEFT);
  label(parent, snapshot.clock.date.c_str(),
        {left.x + 3, left.y + 64, left.width - 6, 22}, small_font());
  label(parent, "SYNCED  0m     Wi-Fi OFF",
        {left.x + 3, left.y + 87, left.width - 6, 18}, small_font());
  divider(parent, {left.x, left.y + 111, left.width, kSeparatorWidth});

  label(parent, "TODAY", {left.x + 3, left.y + 121, 52, 18}, small_font());
  label(parent, "OPEN 09:00", {left.x + 65, left.y + 121, 86, 18}, small_font());
  label(parent, "NEXT 10:00  TAIEX CHECK", {left.x + 3, left.y + 146,
                                             left.width - 6, 23}, medium_font());
  label(parent, snapshot.weather.alert ? "WEATHER ALERT  RAIN 40%"
                                       : "WEATHER CLEAR",
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
  label(parent, "REFLECTIVE MONO  /  UPDATE AGE 0m", {left.x + 3, left.y + 243,
                                                       left.width - 6, 18},
        small_font());

  render_right_tiles(parent, snapshot, right);
  page_dots(parent, active_page, bounds);
}

}  // namespace ui
