#include "ui_app.hpp"

#include <cstdio>
#include <string>

namespace ui {
namespace {

const lv_font_t* small_font() { return &lv_font_montserrat_14; }
const lv_font_t* medium_font() { return &lv_font_montserrat_20; }

void context_host_deleted(lv_event_t* event) {
  auto* context = static_cast<UiContext*>(lv_event_get_user_data(event));
  if (context == nullptr) return;
  context->host = nullptr;
  context->root = nullptr;
  context->initialized = false;
}

#ifndef NDEBUG
void assert_tree_in_safe_canvas(lv_obj_t* object) {
  lv_obj_update_layout(object);
  lv_area_t area{};
  lv_obj_get_coords(object, &area);
  const Rect safe = safe_canvas();
  LV_ASSERT_MSG(area.x1 >= safe.x && area.y1 >= safe.y &&
                    area.x2 < safe.right() && area.y2 < safe.bottom(),
                "page object outside 6px safe canvas");
  const uint32_t child_count = lv_obj_get_child_count(object);
  for (uint32_t index = 0; index < child_count; ++index) {
    assert_tree_in_safe_canvas(lv_obj_get_child(object, index));
  }
}
#endif

void tile(lv_obj_t* parent, const char* title, const char* value,
          const char* detail, Rect bounds, bool weather, bool indoor) {
#ifndef NDEBUG
  LV_ASSERT_MSG(tile_content_is_centered(bounds),
                "right tile content is not vertically centered");
  LV_ASSERT_MSG(tile_content_has_no_reserved_footer(bounds),
                "right tile has a reserved footer band");
#endif
  const Rect content = tile_content_rect(bounds);
  label(parent, title, {bounds.x + 6, bounds.y + 8, bounds.width - 12, 18},
        small_font(), LV_TEXT_ALIGN_LEFT);
  if (weather) {
    weather_icon(parent, {bounds.x + 8, content.y + 2, 30, 30}, false);
  } else if (indoor) {
    temperature_icon(parent, {bounds.x + 8, content.y + 2, 30, 30});
  } else {
    line_segment(parent, bounds.x + 8, content.y + 31, bounds.width - 16, 1);
    line_segment(parent, bounds.x + 10, content.y + 28, bounds.width / 3, 1);
  }
  const int value_x = bounds.x + (weather || indoor ? 43 : 6);
  const int value_w = bounds.width - (weather || indoor ? 49 : 12);
  label(parent, value, {value_x, content.y, value_w, 28}, medium_font(),
        LV_TEXT_ALIGN_CENTER);
  label(parent, detail, {bounds.x + 6, content.y + 28,
                         bounds.width - 12, 16},
        small_font(), LV_TEXT_ALIGN_CENTER);
}

}  // namespace

bool init_context(UiContext& context, lv_obj_t* host) {
  if (host == nullptr) return false;
  if (context.initialized) return context.host == host;
  context.host = host;
  context.root = nullptr;
  context.initialized = true;
  lv_obj_add_event_cb(host, context_host_deleted, LV_EVENT_DELETE, &context);
  return true;
}

void reset_context(UiContext& context) {
  if (context.host != nullptr) {
    lv_obj_remove_event_cb_with_user_data(context.host, context_host_deleted,
                                           &context);
  }
  if (context.root != nullptr) {
    lv_obj_delete(context.root);
  }
  context.host = nullptr;
  context.root = nullptr;
  context.initialized = false;
}

void render_right_tiles(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                        Rect bounds) {
  const auto cells = right_tile_cells(bounds);
  const Rect& weather = cells[0];
  const Rect& indoor = cells[1];
  const Rect& market = cells[2];
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

void render_market_sidebar(lv_obj_t* parent,
                           const app_core::AppSnapshot& snapshot,
                           const app_core::MarketData& market, Rect bounds,
                           bool us_market) {
  const auto cells = right_tile_cells(bounds);
  const Rect& index = cells[0];
  const Rect& weather = cells[1];
  const Rect& indoor = cells[2];
  char index_value[24];
  char index_detail[24];
  char weather_value[24];
  char weather_detail[24];
  char indoor_value[24];
  char indoor_detail[24];
  const auto& weather_snapshot =
      us_market ? snapshot.new_york_weather : snapshot.weather;
  std::snprintf(index_value, sizeof(index_value), "%d", market.secondary_value);
  std::snprintf(index_detail, sizeof(index_detail), "%+.2f%%",
                market.secondary_change_percent);
  std::snprintf(weather_value, sizeof(weather_value), "%.0f C",
                weather_snapshot.current.temperature_c);
  std::snprintf(weather_detail, sizeof(weather_detail), "%s %u%%",
                weather_snapshot.current.condition.c_str(),
                weather_snapshot.current.rain_probability_percent);
  std::snprintf(indoor_value, sizeof(indoor_value), "%.1f C",
                snapshot.indoor.temperature_c);
  std::snprintf(indoor_detail, sizeof(indoor_detail), "RH %u%%",
                snapshot.indoor.humidity_percent);
  tile(parent, market.secondary_label.c_str(), index_value, index_detail, index,
       false, false);
  tile(parent, weather_snapshot.current.location.c_str(), weather_value,
       weather_detail, weather, true, false);
  tile(parent, "INDOOR", indoor_value, indoor_detail, indoor, false, true);
  divider(parent, {bounds.x, index.bottom(), bounds.width, kSeparatorWidth});
  divider(parent, {bounds.x, weather.bottom(), bounds.width, kSeparatorWidth});
}

void render_mast(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds) {
  const std::string clock = format_minute_clock(snapshot.clock.hero);
  const std::string source = snapshot.clock.source.empty()
                                 ? "SOURCE UNKNOWN"
                                 : snapshot.clock.source;
  char indoor_summary[32];
  std::snprintf(indoor_summary, sizeof(indoor_summary), "%.1f  %u%%",
                snapshot.indoor.temperature_c,
                snapshot.indoor.humidity_percent);
  label(parent, clock.c_str(), {bounds.x, bounds.y, 60, 25}, medium_font(),
        LV_TEXT_ALIGN_LEFT);
  std::string context = "SNAPSHOT / " + source;
  label(parent, context.c_str(), {bounds.x + 64, bounds.y + 3, 155, 18},
        small_font());
  label(parent, "Wi-Fi OFF", {bounds.x + 221, bounds.y + 3, 70, 18},
        small_font());
  label(parent, indoor_summary,
        {bounds.x + 294, bounds.y + 3, bounds.width - 294, 18}, small_font(),
        LV_TEXT_ALIGN_RIGHT);
}

lv_obj_t* render_page(UiContext& context,
                      const app_core::AppSnapshot& snapshot,
                      app_core::PageId page, Rect bounds,
                      std::size_t page_index, std::size_t page_count) {
  if (!context_ready(context) || !within_safe_canvas(bounds)) return nullptr;

  // LVGL screens are parentless and therefore form a genuinely detached
  // staging surface. The page root is moved to the host only after rendering.
  lv_obj_t* staging_screen = lv_obj_create(nullptr);
  if (staging_screen == nullptr) return nullptr;
  lv_obj_set_size(staging_screen, kCanvasWidth, kCanvasHeight);
  lv_obj_t* replacement = lv_obj_create(staging_screen);
  if (replacement == nullptr) {
    lv_obj_delete(staging_screen);
    return nullptr;
  }
  lv_obj_add_flag(replacement, LV_OBJ_FLAG_HIDDEN);
  apply_surface(replacement);
  lv_obj_set_pos(replacement, bounds.x, bounds.y);
  lv_obj_set_size(replacement, bounds.width, bounds.height);

#ifndef NDEBUG
  LV_ASSERT_MSG(within_safe_canvas(bounds),
                "replacement root outside safe canvas");
#endif

  const Rect local_bounds{0, 0, bounds.width, bounds.height};

  switch (page) {
    case app_core::PageId::TaiwanMarket:
      render_market(replacement, snapshot, snapshot.taiwan_market, local_bounds,
                    page_index, page_count, false);
      break;
    case app_core::PageId::UsMarket:
      render_market(replacement, snapshot, snapshot.us_market, local_bounds,
                    page_index, page_count, true);
      break;
    case app_core::PageId::Weather:
      render_weather(replacement, snapshot, local_bounds, page_index, page_count);
      break;
    case app_core::PageId::Indoor:
      render_indoor(replacement, snapshot, local_bounds, page_index, page_count);
      break;
    case app_core::PageId::Home:
    default:
      render_home(replacement, snapshot, local_bounds, page_index, page_count);
      break;
  }
  lv_obj_set_parent(replacement, context.host);
  lv_obj_delete(staging_screen);
  if (context.root != nullptr && context.root != replacement) {
    lv_obj_delete(context.root);
  }
  context.root = replacement;
  lv_obj_clear_flag(replacement, LV_OBJ_FLAG_HIDDEN);
#ifndef NDEBUG
  assert_tree_in_safe_canvas(replacement);
#endif
  return replacement;
}

lv_obj_t* navigation_overlay(UiContext& context, Rect bounds) {
  if (!context_ready(context) || context.root == nullptr) return nullptr;
  return ui::navigation_overlay(context.root, bounds);
}

}  // namespace ui
