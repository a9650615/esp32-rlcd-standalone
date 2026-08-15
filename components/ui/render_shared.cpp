#include "ui_app.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef NDEBUG
#include <esp_log.h>
#endif

namespace ui {
namespace {

#ifndef NDEBUG
constexpr char kTag[] = "ui_geometry";
#endif

const lv_font_t* small_font() { return &lv_font_montserrat_14; }
const lv_font_t* medium_font() { return &lv_font_montserrat_20; }

void context_host_deleted(lv_event_t* event) {
  auto* context = static_cast<UiContext*>(lv_event_get_user_data(event));
  if (context == nullptr) return;
  context->host = nullptr;
  context->root = nullptr;
  context->clock_label = nullptr;
  context->network_label = nullptr;
  context->battery_label = nullptr;
  context->setup_status_label = nullptr;
  context->staging_clock_label = nullptr;
  context->staging_network_label = nullptr;
  context->staging_battery_label = nullptr;
  context->staging_setup_status_label = nullptr;
  context->initialized = false;
}

// Compares against the label's current text and skips lv_label_set_text
// entirely when unchanged - lv_label_set_text always reallocates and
// invalidates even when passed a byte-identical string, so this comparison
// is what actually makes an unchanged value cost no redraw at all.
void set_label_text_if_changed(lv_obj_t* label_obj, const char* text) {
  if (label_obj == nullptr || text == nullptr) return;
  const char* current = lv_label_get_text(label_obj);
  if (current != nullptr && std::strcmp(current, text) == 0) return;
  lv_label_set_text(label_obj, text);
}

// Same skip-when-unchanged discipline as set_label_text_if_changed above,
// but for the Setup status label specifically: a text change also needs its
// error styling (the inverted bar) re-evaluated, since a status-only publish
// - not a full page rebuild - is what carries a neutral status flipping to a
// failure (or back) while Setup stays on screen. `error` comes straight from
// app_core::SetupData::error; the status wording and its failure/neutral
// classification are published together by wifi_provision on every state
// change, so gating both on the text comparison is sufficient in practice.
void set_setup_status_if_changed(lv_obj_t* label_obj,
                                 const std::string& status_text, bool error) {
  if (label_obj == nullptr) return;
  const char* current = lv_label_get_text(label_obj);
  if (current != nullptr && status_text == current) return;
  lv_label_set_text(label_obj, status_text.c_str());
  apply_setup_status_style(label_obj, error);
}

#ifndef NDEBUG
// Walks the tree reading already-computed coordinates only - the one layout
// pass that makes those coordinates valid happens once, before the walk
// starts (see assert_tree_in_safe_canvas below). Calling lv_obj_update_layout
// per node here would re-run a full-tree layout recalculation at every node,
// turning an N-object page into N full layout passes; that O(N^2) cost is
// what stalled the LVGL task long enough to starve IDLE0 and trip the task
// watchdog once the Setup page's QR widget pushed the object count up.
//
// It reports instead of asserting. LV_ASSERT_MSG calls LVGL's assert handler,
// whose default body is an infinite loop, so on device the first out-of-bounds
// object hung the LVGL task forever and bricked the UI - with no message at
// all, because LV_USE_LOG is off. The observed symptom was a task watchdog
// every 5 s starting the moment a button press rendered the first page
// carrying the tray. A geometry bug is worth a loud log line, not a dead
// display. The layout rects themselves are already proven by static_assert in
// ui_data.hpp; this walk exists to catch runtime growth those cannot see, such
// as a label auto-sizing past the box it was given.
void assert_tree_in_safe_canvas_walk(lv_obj_t* object, int page) {
  lv_area_t area{};
  lv_obj_get_coords(object, &area);
  const Rect safe = safe_canvas();
  if (area.x1 < safe.x || area.y1 < safe.y || area.x2 >= safe.right() ||
      area.y2 >= safe.bottom()) {
    ESP_LOGW(kTag,
             "page=%d object outside safe canvas: x1=%d y1=%d x2=%d y2=%d "
             "(safe x=%d y=%d right=%d bottom=%d)",
             page, static_cast<int>(area.x1), static_cast<int>(area.y1),
             static_cast<int>(area.x2), static_cast<int>(area.y2), safe.x,
             safe.y, safe.right(), safe.bottom());
  }
  const uint32_t child_count = lv_obj_get_child_count(object);
  for (uint32_t index = 0; index < child_count; ++index) {
    assert_tree_in_safe_canvas_walk(lv_obj_get_child(object, index), page);
  }
}

void assert_tree_in_safe_canvas(lv_obj_t* object, int page) {
  lv_obj_update_layout(object);
  assert_tree_in_safe_canvas_walk(object, page);
}
#endif

// `valid` gates the fabricated-number path once, here, for every caller
// (Home's situational tile and the market-page/indoor-page sidebar tiles
// below) instead of each call site inventing its own placeholder: an invalid
// tile shows kNoDataLabel with a blank detail line and no leading icon (an
// icon would itself imply data that is not there), never the caller's
// possibly-default value/detail strings. `condition` selects which of the
// four bold weather silhouettes to draw (see weather_icon_kind_for_condition
// in ui_data.hpp); only consulted when weather is true, so callers that pass
// weather=false can leave it null.
void tile(lv_obj_t* parent, const char* title, const char* value,
          const char* detail, Rect bounds, bool weather, bool indoor,
          bool valid = true, const char* condition = nullptr) {
#ifndef NDEBUG
  // Logged, not asserted: see the tree walk above - LVGL's assert handler
  // never returns, so a layout complaint would cost the whole display.
  if (!tile_content_is_centered(bounds)) {
    ESP_LOGW(kTag, "right tile content not vertically centered: y=%d h=%d",
             bounds.y, bounds.height);
  }
  if (!tile_content_has_no_reserved_footer(bounds)) {
    ESP_LOGW(kTag, "right tile has a reserved footer band: y=%d h=%d", bounds.y,
             bounds.height);
  }
#endif
  const TileTextLayout rows = tile_text_layout(bounds);
  label(parent, title, rows.title, small_font(), LV_TEXT_ALIGN_LEFT);
  const bool has_leading_visual = valid && (weather || indoor);
  const Rect leading_visual =
      tile_leading_visual_rect(bounds, has_leading_visual);
  if (has_leading_visual) {
    if (weather) {
      weather_icon(parent, leading_visual,
                  weather_icon_kind_for_condition(
                      condition != nullptr ? condition : ""));
    } else if (indoor) {
      temperature_icon(parent, leading_visual);
    }
  }
  label(parent, valid ? value : kNoDataLabel,
        tile_value_rect(bounds, has_leading_visual), medium_font(),
        LV_TEXT_ALIGN_CENTER);
  label(parent, valid ? detail : "", rows.detail, small_font(),
        LV_TEXT_ALIGN_CENTER);
}

}  // namespace

bool init_context(UiContext& context, lv_obj_t* host) {
  if (host == nullptr) return false;
  if (context.initialized) return context.host == host;
  context.host = host;
  context.root = nullptr;
  context.clock_label = nullptr;
  context.network_label = nullptr;
  context.battery_label = nullptr;
  context.setup_status_label = nullptr;
  context.staging_clock_label = nullptr;
  context.staging_network_label = nullptr;
  context.staging_battery_label = nullptr;
  context.staging_setup_status_label = nullptr;
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
    context.clock_label = nullptr;
    context.network_label = nullptr;
    context.battery_label = nullptr;
    context.setup_status_label = nullptr;
    lv_obj_delete(context.root);
  }
  context.host = nullptr;
  context.root = nullptr;
  context.clock_label = nullptr;
  context.network_label = nullptr;
  context.battery_label = nullptr;
  context.setup_status_label = nullptr;
  context.staging_clock_label = nullptr;
  context.staging_network_label = nullptr;
  context.staging_battery_label = nullptr;
  context.staging_setup_status_label = nullptr;
  context.initialized = false;
}

// Home's single situational tile (see choose_home_tile in ui_data.hpp):
// whichever candidate wins fills the entire right column instead of sharing
// it with two others, so `bounds` is drawn on directly with no
// right_tile_cells split and no internal dividers.
void render_right_tiles(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                        Rect bounds) {
  const HomeTileKind kind = choose_home_tile(snapshot);
  char value[24] = "";
  char detail[24] = "";
  const char* title = "STATUS";
  const char* condition = nullptr;
  bool weather = false;
  bool indoor = false;
  switch (kind) {
    case HomeTileKind::Battery:
      title = "BATTERY";
      std::snprintf(value, sizeof(value), "%u%%", snapshot.battery.percent);
      std::snprintf(detail, sizeof(detail), "%s",
                    snapshot.battery.overvoltage_warning ? "OVERVOLTAGE"
                                                          : "LOW BATTERY");
      break;
    case HomeTileKind::Weather:
      title = "WEATHER";
      std::snprintf(value, sizeof(value), "%.0f C",
                    snapshot.weather.current.temperature_c);
      std::snprintf(detail, sizeof(detail), "%s%s%s",
                    snapshot.weather.alert ? "ALERT  " : "",
                    snapshot.weather.current.condition.c_str(),
                    snapshot.weather.stale ? kStaleSuffix : "");
      condition = snapshot.weather.current.condition.c_str();
      weather = true;
      break;
    case HomeTileKind::Market:
      title = "MARKET";
      std::snprintf(value, sizeof(value), "%d",
                    snapshot.taiwan_market.primary_value);
      std::snprintf(detail, sizeof(detail), "%+.2f%%",
                    snapshot.taiwan_market.primary_change_percent);
      break;
    case HomeTileKind::Indoor:
      title = "INDOOR";
      std::snprintf(value, sizeof(value), "%.1f C", snapshot.indoor.temperature_c);
      std::snprintf(detail, sizeof(detail), "RH %u%%",
                    snapshot.indoor.humidity_percent);
      indoor = true;
      break;
    case HomeTileKind::None:
      break;
  }
  tile(parent, title, value, detail, bounds, weather, indoor,
       kind != HomeTileKind::None, condition);
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
  std::snprintf(weather_detail, sizeof(weather_detail), "%s %u%%%s",
                weather_snapshot.current.condition.c_str(),
                weather_snapshot.current.rain_probability_percent,
                weather_snapshot.stale ? kStaleSuffix : "");
  std::snprintf(indoor_value, sizeof(indoor_value), "%.1f C",
                snapshot.indoor.temperature_c);
  std::snprintf(indoor_detail, sizeof(indoor_detail), "RH %u%%",
                snapshot.indoor.humidity_percent);
  tile(parent, market.secondary_label.c_str(), index_value, index_detail, index,
       false, false, market.valid);
  tile(parent, weather_snapshot.current.location.c_str(), weather_value,
       weather_detail, weather, true, false, weather_snapshot.valid,
       weather_snapshot.current.condition.c_str());
  tile(parent, "INDOOR", indoor_value, indoor_detail, indoor, false, true,
       snapshot.indoor.valid);
  divider(parent, {bounds.x, index.bottom(), bounds.width, kSeparatorWidth});
  divider(parent, {bounds.x, weather.bottom(), bounds.width, kSeparatorWidth});
}

void render_tray(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds, std::size_t page_index, std::size_t page_count,
                 UiContext* context) {
  const SystemTrayLayout cells = system_tray_layout(bounds);
  const std::string clock = format_minute_clock(snapshot.clock.hero);
  lv_obj_t* clock_label =
      label(parent, clock.c_str(), cells.time, medium_font(),
            LV_TEXT_ALIGN_LEFT);
  if (context != nullptr) context->staging_clock_label = clock_label;

  const std::string network_text = tray_network_text(snapshot.setup);
  lv_obj_t* network_label = label(parent, network_text.c_str(), cells.network,
                                  small_font(), LV_TEXT_ALIGN_CENTER);
  if (context != nullptr) context->staging_network_label = network_label;

  // Unread battery (valid == false) renders a blank cell rather than a
  // misleading "0%" - the board may simply not have sampled it yet. The
  // label is always created (even when blank) so its lv_obj_t* pointer stays
  // valid for the label-only repaint path: a later battery sample just
  // changes this label's text, no page rebuild.
  const std::string battery_text = tray_battery_text(snapshot.battery);
  lv_obj_t* battery_label = label(parent, battery_text.c_str(), cells.battery,
                                  small_font(), LV_TEXT_ALIGN_RIGHT);
  if (context != nullptr) context->staging_battery_label = battery_label;

  // The page-dot indicator moved here from the bottom-right corner of every
  // data page (see render_weather.cpp/render_indoor.cpp/render_market.cpp).
  // Setup calls render_page with (page_index, page_count) == (0, 0), which
  // page_dots_geometry already renders as zero dots.
  page_dots(parent, page_index, page_count, cells.dots);

  divider(parent, {bounds.x, bounds.y + kSystemTrayHeight, bounds.width,
                   kSeparatorWidth});
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
  // Logged, not asserted, for the same reason as the tree walk above: LVGL's
  // assert handler spins forever and takes the display with it.
  if (!within_safe_canvas(bounds)) {
    ESP_LOGW(kTag, "replacement root outside safe canvas: x=%d y=%d w=%d h=%d",
             bounds.x, bounds.y, bounds.width, bounds.height);
  }
#endif

  const Rect local_bounds{0, 0, bounds.width, bounds.height};
  context.staging_clock_label = nullptr;
  context.staging_network_label = nullptr;
  context.staging_battery_label = nullptr;
  context.staging_setup_status_label = nullptr;

  // Single site that decides whether a page carries the tray and, if so,
  // reserves its height - every renderer below just gets `content` and
  // never hand-tunes its own top offset.
  if (page_shows_tray(page)) {
    render_tray(replacement, snapshot,
               {0, 0, local_bounds.width, kSystemTrayHeight}, page_index,
               page_count, &context);
  }
  const Rect content = content_bounds(local_bounds, page);

  switch (page) {
    case app_core::PageId::TaiwanMarket:
      render_market(replacement, snapshot, snapshot.taiwan_market, content,
                    page_index, page_count, false, &context);
      break;
    case app_core::PageId::UsMarket:
      render_market(replacement, snapshot, snapshot.us_market, content,
                    page_index, page_count, true, &context);
      break;
    case app_core::PageId::Weather:
      render_weather(replacement, snapshot, content, page_index, page_count,
                     &context);
      break;
    case app_core::PageId::Indoor:
      render_indoor(replacement, snapshot, content, page_index, page_count,
                    &context);
      break;
    case app_core::PageId::Setup:
      render_setup(replacement, snapshot, content, page_index, page_count,
                   &context);
      break;
    case app_core::PageId::Home:
    default:
      render_home(replacement, snapshot, content, page_index, page_count,
                  &context);
      break;
  }
  lv_obj_set_parent(replacement, context.host);
  lv_obj_delete(staging_screen);
  if (context.root != nullptr && context.root != replacement) {
    context.clock_label = nullptr;
    context.network_label = nullptr;
    context.battery_label = nullptr;
    context.setup_status_label = nullptr;
    lv_obj_delete(context.root);
  }
  context.root = replacement;
  context.clock_label = context.staging_clock_label;
  context.network_label = context.staging_network_label;
  context.battery_label = context.staging_battery_label;
  context.setup_status_label = context.staging_setup_status_label;
  context.staging_clock_label = nullptr;
  context.staging_network_label = nullptr;
  context.staging_battery_label = nullptr;
  context.staging_setup_status_label = nullptr;
  lv_obj_clear_flag(replacement, LV_OBJ_FLAG_HIDDEN);
#ifndef NDEBUG
  // PageId ordinal, not a name: page_name() lives in another translation
  // unit's anonymous namespace and is not worth widening a public header for
  // one diagnostic. Order is Home, TaiwanMarket, UsMarket, Weather, Indoor,
  // Setup.
  assert_tree_in_safe_canvas(replacement, static_cast<int>(page));
#endif
  return replacement;
}

bool update_visible_clock(UiContext& context,
                          const app_core::AppSnapshot& snapshot) {
  if (!context_ready(context) || context.clock_label == nullptr) return false;
  set_label_text_if_changed(context.clock_label,
                            format_minute_clock(snapshot.clock.hero).c_str());
  return true;
}

bool update_visible_fields(UiContext& context,
                           const app_core::AppSnapshot& snapshot) {
  if (!context_ready(context)) return false;
  update_visible_clock(context, snapshot);
  set_label_text_if_changed(context.network_label,
                            tray_network_text(snapshot.setup).c_str());
  set_label_text_if_changed(context.battery_label,
                            tray_battery_text(snapshot.battery).c_str());
  set_setup_status_if_changed(context.setup_status_label,
                              setup_status_text(snapshot.setup.status),
                              snapshot.setup.error);
  return true;
}

lv_obj_t* navigation_overlay(UiContext& context, Rect bounds) {
  if (!context_ready(context) || context.root == nullptr) return nullptr;
  return ui::navigation_overlay(context.root, bounds);
}

}  // namespace ui
