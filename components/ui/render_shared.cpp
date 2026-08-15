#include "ui_app.hpp"

#include <cstdio>

namespace ui {
namespace {

const lv_font_t* small_font() { return &lv_font_montserrat_14; }
const lv_font_t* medium_font() { return &lv_font_montserrat_20; }

void tile(lv_obj_t* parent, const char* title, const char* value,
          const char* detail, Rect bounds, bool weather, bool indoor) {
  label(parent, title, {bounds.x + 6, bounds.y + 8, bounds.width - 12, 18},
        small_font(), LV_TEXT_ALIGN_LEFT);
  if (weather) {
    weather_icon(parent, {bounds.x + 8, bounds.y + 32, 30, 30}, false);
  } else if (indoor) {
    temperature_icon(parent, {bounds.x + 8, bounds.y + 32, 30, 30});
  } else {
    line_segment(parent, bounds.x + 8, bounds.y + 61, bounds.width - 16, 1);
    line_segment(parent, bounds.x + 10, bounds.y + 58, bounds.width / 3, 1);
  }
  const int value_x = bounds.x + (weather || indoor ? 43 : 6);
  const int value_w = bounds.width - (weather || indoor ? 49 : 12);
  label(parent, value, {value_x, bounds.y + 31, value_w, 28}, medium_font(),
        LV_TEXT_ALIGN_CENTER);
  label(parent, detail, {bounds.x + 6, bounds.y + bounds.height - 24,
                         bounds.width - 12, 16},
        small_font(), LV_TEXT_ALIGN_CENTER);
}

}  // namespace

void render_right_tiles(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                        Rect bounds) {
  const int cell_height = (bounds.height - 2 * kSeparatorWidth) / 3;
  const Rect weather{bounds.x, bounds.y, bounds.width, cell_height};
  const Rect indoor{bounds.x, bounds.y + cell_height + kSeparatorWidth,
                    bounds.width, cell_height};
  const Rect market{bounds.x,
                    bounds.y + 2 * (cell_height + kSeparatorWidth),
                    bounds.width, bounds.height - 2 * (cell_height + kSeparatorWidth)};
  char weather_value[24];
  char indoor_value[24];
  char market_value[24];
  char weather_detail[24];
  char indoor_detail[24];
  char market_detail[24];
  std::snprintf(weather_value, sizeof(weather_value), "%.0f C",
                snapshot.weather.current.temperature_c);
  std::snprintf(weather_detail, sizeof(weather_detail), "%s %u%%",
                snapshot.weather.current.condition.c_str(),
                snapshot.weather.current.rain_probability_percent);
  std::snprintf(indoor_value, sizeof(indoor_value), "%.1f C",
                snapshot.indoor.temperature_c);
  std::snprintf(indoor_detail, sizeof(indoor_detail), "RH %u%%",
                snapshot.indoor.humidity_percent);
  std::snprintf(market_value, sizeof(market_value), "%d",
                snapshot.taiwan_market.primary_value);
  std::snprintf(market_detail, sizeof(market_detail), "%+.2f%%",
                snapshot.taiwan_market.primary_change_percent);
  tile(parent, "WEATHER", weather_value, weather_detail, weather, true, false);
  tile(parent, "INDOOR", indoor_value, indoor_detail, indoor, false, true);
  tile(parent, "MARKET", market_value, market_detail, market, false, false);
  divider(parent, {bounds.x, weather.bottom(), bounds.width, kSeparatorWidth});
  divider(parent, {bounds.x, indoor.bottom(), bounds.width, kSeparatorWidth});
}

void render_mast(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds) {
  label(parent, "RLCD", {bounds.x, bounds.y, 42, 18}, small_font());
  label(parent, snapshot.clock.source.c_str(), {bounds.x + 48, bounds.y,
                                                  bounds.width - 48, 18},
        small_font(), LV_TEXT_ALIGN_RIGHT);
}

lv_obj_t* render_page(lv_obj_t* host, const app_core::AppSnapshot& snapshot,
                      app_core::PageId page, Rect bounds,
                      uint8_t active_page) {
  if (host == nullptr || !within_safe_canvas(bounds)) return nullptr;

  static lv_obj_t* active_root = nullptr;
  lv_obj_t* replacement = lv_obj_create(host);
  if (replacement == nullptr) return nullptr;
  lv_obj_add_flag(replacement, LV_OBJ_FLAG_HIDDEN);
  apply_surface(replacement);
  lv_obj_set_pos(replacement, bounds.x, bounds.y);
  lv_obj_set_size(replacement, bounds.width, bounds.height);

#ifndef NDEBUG
  LV_ASSERT_MSG(within_safe_canvas(bounds),
                "replacement root outside safe canvas");
#endif

  const Rect local_bounds{0, 0, bounds.width, bounds.height};

  // Task 5 owns the home shell. Other pages intentionally fall back to the
  // same shell until their dedicated renderers land in Task 6.
  switch (page) {
    case app_core::PageId::Home:
    default:
      render_home(replacement, snapshot, local_bounds, active_page);
      break;
  }
  if (active_root != nullptr && active_root != replacement) {
    lv_obj_delete(active_root);
  }
  active_root = replacement;
  lv_obj_clear_flag(replacement, LV_OBJ_FLAG_HIDDEN);
  return replacement;
}

}  // namespace ui
