#include "ui_app.hpp"
#include "ui_fonts.hpp"

#include <cstdio>
#include <cstring>
#include <string>

// Unconditional, not #ifndef NDEBUG like the tag right below: the tray
// state-change log a few lines down runs in every build, not just debug
// ones - see update_visible_fields().
#include <esp_log.h>

namespace ui {
namespace {

#ifndef NDEBUG
constexpr char kTag[] = "ui_geometry";
#endif
// Separate from kTag above (which is debug-build-only, for the safe-canvas
// tree walk): this one is used unconditionally.
constexpr char kTrayTag[] = "ui_tray";

const lv_font_t* small_font() { return font_small(); }
const lv_font_t* medium_font() { return font_medium(); }
const lv_font_t* large_font() { return font_large(); }

void clear_tray_indicator_icons(TrayIndicatorIcon* icons) {
  for (int i = 0; i < app_core::kMaxTrayIndicators; ++i) icons[i] = {};
}

// Last-logged active state per registry slot, so log_tray_indicator_state
// below records only genuine transitions - not a line every ~100 ms tick.
// File-scope, not in UiContext: a page rebuild recreates the context, and
// this must not manufacture a spurious "changed" line just because of
// that. Two arrays rather than one signed tri-state, so "never logged yet"
// (log the very first observed value, whatever it is) is distinguishable
// from "logged and it was false" without a sentinel value.
bool g_tray_indicator_ever_logged[app_core::kMaxTrayIndicators] = {};
bool g_tray_indicator_last_logged_active[app_core::kMaxTrayIndicators] = {};

// The point where the tray actually consumes each registered indicator's
// activity state - added specifically because two earlier attempts at this
// feature both failed silently on hardware with no way to tell "the value
// never reached here" from "it arrived and nothing was drawn". Logged
// every tick regardless of whether context_ready() below finds anything
// to draw into, so a registered-but-never-rendered slot still shows up
// here - that distinction is exactly what answers the open question.
void log_tray_indicator_state_changes() {
  for (int i = 0; i < app_core::kMaxTrayIndicators; ++i) {
    const app_core::TrayIndicatorSlot slot = app_core::tray_indicator_slot(i);
    if (!slot.registered) continue;
    if (g_tray_indicator_ever_logged[i] &&
        slot.active == g_tray_indicator_last_logged_active[i]) {
      continue;
    }
    ESP_LOGI(kTrayTag, "tray indicator slot %d -> %s", i,
             slot.active ? "active" : "inactive");
    g_tray_indicator_ever_logged[i] = true;
    g_tray_indicator_last_logged_active[i] = slot.active;
  }
}

void context_host_deleted(lv_event_t* event) {
  auto* context = static_cast<UiContext*>(lv_event_get_user_data(event));
  if (context == nullptr) return;
  context->host = nullptr;
  context->root = nullptr;
  context->clock_label = nullptr;
  context->network_icon = {};
  context->battery_icon_parts = {};
  clear_tray_indicator_icons(context->tray_indicator_icons);
  context->setup_status_label = nullptr;
  context->staging_clock_label = nullptr;
  context->staging_network_icon = {};
  context->staging_battery_icon = {};
  clear_tray_indicator_icons(context->staging_tray_indicator_icons);
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
  // Centering is the whole check: content centred in its cell leaves equal
  // space above and below, so a dead band can only appear if centring fails.
  // A separate footer test used to sit here and required the cell to be under
  // three times the content height - true of the old three-tile sidebar, never
  // of Home's single tall tile, so it warned once per render for a layout that
  // was correct.
  if (!tile_content_is_centered(bounds)) {
    ESP_LOGW(kTag, "right tile content not vertically centered: y=%d h=%d",
             bounds.y, bounds.height);
  }
#endif
  const TileTextLayout rows = tile_text_layout(bounds);
  label(parent, title, rows.title, medium_font(), LV_TEXT_ALIGN_LEFT);
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
  label(parent, valid ? value : text(Text::NoData),
        tile_value_rect(bounds, has_leading_visual), large_font(),
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
  context.network_icon = {};
  context.battery_icon_parts = {};
  clear_tray_indicator_icons(context.tray_indicator_icons);
  context.setup_status_label = nullptr;
  context.staging_clock_label = nullptr;
  context.staging_network_icon = {};
  context.staging_battery_icon = {};
  clear_tray_indicator_icons(context.staging_tray_indicator_icons);
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
    context.network_icon = {};
    context.battery_icon_parts = {};
    clear_tray_indicator_icons(context.tray_indicator_icons);
    context.setup_status_label = nullptr;
    lv_obj_delete(context.root);
  }
  context.host = nullptr;
  context.root = nullptr;
  context.clock_label = nullptr;
  context.network_icon = {};
  context.battery_icon_parts = {};
  clear_tray_indicator_icons(context.tray_indicator_icons);
  context.setup_status_label = nullptr;
  context.staging_clock_label = nullptr;
  context.staging_network_icon = {};
  context.staging_battery_icon = {};
  clear_tray_indicator_icons(context.staging_tray_indicator_icons);
  context.staging_setup_status_label = nullptr;
  context.initialized = false;
}

// Home's single situational tile (see choose_home_tile in ui_data.hpp):
// whichever candidate wins fills the entire right column instead of sharing
// it with two others, so `bounds` is drawn on directly with no
// right_tile_cells split and no internal dividers.
// Draws one Home tile of the given kind into `bounds`.
void render_home_tile(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                      HomeTileKind kind, Rect bounds) {
  char value[24] = "";
  char detail[24] = "";
  const char* title = text(Text::TileStatus);
  const char* condition = nullptr;
  bool weather = false;
  bool indoor = false;
  switch (kind) {
    case HomeTileKind::Battery:
      title = text(Text::TileBattery);
      // battery_percent_trustworthy(), the same gate the tray icon and the
      // settings row use: this tile is otherwise the third place on the
      // same screen a charging cell's percentage would have read as a
      // confident, wrong number while those two already said "Charging".
      //
      // choose_home_tile()/home_battery_notable() are unchanged in this
      // pass on purpose - only what this tile prints once chosen. Whether
      // an untrustworthy percentage should still be able to make the tile
      // flap in as "notable" while sagging past kHomeLowBatteryPercent
      // right after being unplugged is a real question, but there is no
      // evidence yet that it actually happens, and changing tile-priority
      // behaviour on a hunch risks trading this defect for a different one.
      if (battery_percent_trustworthy(snapshot.battery,
                                      snapshot.battery_runtime.trend)) {
        std::snprintf(value, sizeof(value), "%u%%", snapshot.battery.percent);
      } else {
        std::snprintf(value, sizeof(value), "%s", text(Text::StatusCharging));
      }
      std::snprintf(detail, sizeof(detail), "%s",
                    snapshot.battery.overvoltage_warning
                        ? text(Text::StatusOvervoltage)
                        : text(Text::StatusLowBattery));
      break;
    case HomeTileKind::Weather:
      title = text(Text::TileWeather);
      std::snprintf(value, sizeof(value), "%s",
                    temperature_text(snapshot.weather.current.temperature_c, 0)
                        .c_str());
      std::snprintf(detail, sizeof(detail), "%s%s%s",
                    snapshot.weather.alert ? text(Text::StatusAlert) : "",
                    snapshot.weather.current.condition.c_str(),
                    snapshot.weather.stale ? text(Text::StaleSuffix) : "");
      condition = snapshot.weather.current.condition.c_str();
      weather = true;
      break;
    case HomeTileKind::Market:
      title = text(Text::TileMarket);
      std::snprintf(value, sizeof(value), "%d",
                    snapshot.taiwan_market.primary_value);
      std::snprintf(detail, sizeof(detail), "%+.2f%%",
                    snapshot.taiwan_market.primary_change_percent);
      break;
    case HomeTileKind::Indoor:
      title = text(Text::TileIndoor);
      std::snprintf(value, sizeof(value), "%s",
                    temperature_text(snapshot.indoor.temperature_c, 1).c_str());
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

void render_right_tiles(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                        Rect bounds) {
  const HomeTileKind first = choose_home_tile(snapshot);
  const HomeTileKind second = choose_home_second_tile(snapshot, first);
  const int count = second == HomeTileKind::None ? 1 : 2;
  render_home_tile(parent, snapshot, first, home_tile_column(bounds, 0, count));
  if (count == 2) {
    const Rect left = home_tile_column(bounds, 0, count);
    // A rule between them, so two tiles read as two readings rather than one
    // run-on block.
    divider(parent, {left.right() + kStackedTileGap / 2, bounds.y + 4,
                     kSeparatorWidth, bounds.height - 8});
    render_home_tile(parent, snapshot, second,
                     home_tile_column(bounds, 1, count));
  }
}

void render_market_sidebar(lv_obj_t* parent,
                           const app_core::AppSnapshot& snapshot,
                           const app_core::MarketData& market, Rect bounds,
                           bool us_market) {
  (void)snapshot;
  (void)us_market;
  // Market facts only. This column used to carry weather and the onboard
  // sensor as well, which put the same two readings on four different pages in
  // three different shapes - and made "what is this page about?" a question
  // rather than an answer. Weather and the sensor have their own pages, and
  // Home is where a mixed summary belongs.
  //
  // What is left is what the main area has not already said: the secondary
  // index, and the day's range when the provider gave an intraday series.
  // Both are optional now, not just the range - Taiwan's Yahoo primary
  // (market.cpp's refresh_taiwan()) answers one symbol per request and
  // supplies no secondary index at all, unlike the TWSE fallback (TAIEX +
  // TW50 from one response) or the US source (two requests, two indices).
  // An empty secondary_label means "this source did not give us one", not
  // "the value is genuinely zero" - showing it anyway would be a fabricated
  // "0 / +0.00%" tile on a market that is actually open and moving.
  const bool has_secondary = market.valid && !market.secondary_label.empty();
  const bool has_range = market.valid && market.has_intraday;
  const int count = (has_secondary ? 1 : 0) + (has_range ? 1 : 0);
  if (count == 0) return;

  int slot = 0;
  if (has_secondary) {
    char index_value[24];
    char index_detail[24];
    std::snprintf(index_value, sizeof(index_value), "%d",
                  market.secondary_value);
    std::snprintf(index_detail, sizeof(index_detail), "%+.2f%%",
                  market.secondary_change_percent);
    tile(parent, market.secondary_label.c_str(), index_value, index_detail,
         stacked_tile_cell(bounds, slot, count), false, false, market.valid);
    ++slot;
  }

  if (has_range) {
    const MarketRange range = market_intraday_range(
        market.intraday_samples, market.intraday_sample_count);
    char range_value[24];
    char range_detail[24];
    std::snprintf(range_value, sizeof(range_value), "%d", range.high);
    std::snprintf(range_detail, sizeof(range_detail), "%d", range.low);
    const Rect cell = stacked_tile_cell(bounds, slot, count);
    tile(parent, text(Text::TileRange), range_value, range_detail, cell, false,
         false, true);
    if (has_secondary) {
      divider(parent, {bounds.x, cell.y - kStackedTileGap / 2, bounds.width,
                       kSeparatorWidth});
    }
  }
}


void render_tray(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds, std::size_t page_index, std::size_t page_count,
                 UiContext* context, app_core::PageId page) {
  const bool home = page == app_core::PageId::Home;
  // Every registered slot's cell is reserved unconditionally, keyed only on
  // "is anything registered here" (a session-static fact - modules
  // register once, at startup) and the registered bitmap's own width, not
  // on the moment-to-moment active flag. That live flag only ever decides
  // *visibility*, toggled in place every ~100 ms tick by
  // update_visible_fields() below - the same cheap-update path the wifi
  // and battery icons already use. A reservation that depended on the live
  // value would only update at the next full rebuild, which is what
  // previously let an icon show whatever the flag happened to be at render
  // time - stale, not the truth.
  TrayIndicators indicators{};
  for (int i = 0; i < app_core::kMaxTrayIndicators; ++i) {
    const app_core::TrayIndicatorSlot slot = app_core::tray_indicator_slot(i);
    indicators.entries[i] = {slot.registered, slot.registered ? slot.bitmap.width : 0};
  }
  const SystemTrayLayout cells = system_tray_layout(bounds, page, indicators);
  // Home puts the date in the tray's first cell instead of the time. The hero
  // clock two rows below is already the time, and repeating it in 20px type
  // spends the one cell that could say something else. It also takes the date
  // out of the hero block, which is what was crowding the clock.
  //
  // Only the clock label is registered with the context: the label-only
  // repaint path exists for the minute rollover, and the date does not change
  // on a minute boundary.
  const std::string leading =
      home ? snapshot.clock.date : format_minute_clock(snapshot.clock.hero);
  lv_obj_t* clock_label =
      label(parent, leading.c_str(), cells.time, medium_font(),
            LV_TEXT_ALIGN_LEFT);
  if (context != nullptr && !home) context->staging_clock_label = clock_label;

  // Shapes, not words. "WIFI"/"NO WIFI"/"BAT 91%" spent most of the tray on
  // two facts a glance can carry, and the exact charge figure now lives on the
  // settings page where it is the number a calibration is compared against.
  const WifiIconParts wifi =
      wifi_icon(parent, cells.network, snapshot.setup.connected);
  // valid and charging passed separately, not collapsed into one bool: an
  // invalid reading draws an empty body (nothing measured); charging covers
  // the level bar with a solid field with the bolt knocked out of it (a
  // real reading exists, it is just not necessarily a trustworthy level,
  // which is also why the level is not worth showing alongside the bolt) -
  // see battery_icon()'s own comment.
  const BatteryIconParts battery = battery_icon(
      parent, cells.battery, snapshot.battery.percent, snapshot.battery.valid,
      battery_is_charging(snapshot.battery, snapshot.battery_runtime.trend));
  TrayIndicatorIcon indicator_icons[app_core::kMaxTrayIndicators]{};
  for (int i = 0; i < app_core::kMaxTrayIndicators; ++i) {
    // Only drawn when system_tray_layout() actually gave this slot room -
    // a zero-width cell means the tray genuinely does not have space for
    // it (see the drop rule on system_tray_layout()), not that the module
    // is currently inactive; drawing into a zero-width Rect would place an
    // icon on top of whatever cell happens to sit at that x. The *initial*
    // on/off look comes from the slot's own active flag here, at
    // construction time; set_tray_indicator_icon_visible() in
    // update_visible_fields() keeps it correct afterward every ~100 ms
    // tick.
    if (cells.indicators[i].width <= 0) continue;
    const app_core::TrayIndicatorSlot slot = app_core::tray_indicator_slot(i);
    indicator_icons[i] = tray_indicator_icon(parent, cells.indicators[i], i, slot.bitmap);
    set_tray_indicator_icon_visible(indicator_icons[i], slot.active);
  }
  if (context != nullptr) {
    context->staging_network_icon = wifi;
    context->staging_battery_icon = battery;
    for (int i = 0; i < app_core::kMaxTrayIndicators; ++i) {
      context->staging_tray_indicator_icons[i] = indicator_icons[i];
    }
  }

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
  context.staging_network_icon = {};
  context.staging_battery_icon = {};
  clear_tray_indicator_icons(context.staging_tray_indicator_icons);
  context.staging_setup_status_label = nullptr;

  // Single site that decides whether a page carries the tray and, if so,
  // reserves its height - every renderer below just gets `content` and
  // never hand-tunes its own top offset.
  if (page_shows_tray(page)) {
    render_tray(replacement, snapshot,
               {0, 0, local_bounds.width, kSystemTrayHeight}, page_index,
               page_count, &context, page);
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
    case app_core::PageId::Settings:
      render_settings(replacement, snapshot, content, page_index, page_count,
                      &context);
      break;
    case app_core::PageId::NowPlaying:
      render_now_playing(replacement, snapshot, content, page_index, page_count,
                         &context);
      break;
    case app_core::PageId::Ota:
      render_ota(replacement, snapshot, content, page_index, page_count,
                 &context);
      break;
    case app_core::PageId::Home:
    default:
      render_home(replacement, snapshot, content, page_index, page_count,
                  &context);
      break;
  }
  // One band along the bottom, filled according to what the page is: position
  // in the rotation for a carousel page, what the buttons currently do for a
  // page where they mean something else. Drawn here rather than by each
  // renderer so it cannot be forgotten, and against the full page bounds
  // rather than the reduced content area, so it sits identically everywhere.
  //
  // local_bounds, not bounds: children are positioned relative to the page
  // root, which is itself already placed at the canvas origin. Passing the
  // absolute rect here put the dots at 6 + 289 = 295, five pixels below the
  // safe canvas - which is what the geometry walk caught on the first boot
  // after this was written.
  if (page_shows_dots(page)) {
    page_dots(replacement, page_index, page_count,
              page_dots_band(local_bounds));
  } else if (page == app_core::PageId::Settings) {
    // The hint band is not decoration here. It is the only thing telling
    // anyone that KEY has stopped turning pages and started moving a cursor.
    button_hints(replacement, page_dots_band(local_bounds),
                 input_hints(InputContext::Menu));
  }
#ifndef NDEBUG
  // After the tree is complete but before it is handed to the host, so the
  // snapshot is of a finished page.
  lv_obj_update_layout(replacement);
#endif
  lv_obj_set_parent(replacement, context.host);
  lv_obj_delete(staging_screen);
  if (context.root != nullptr && context.root != replacement) {
    context.clock_label = nullptr;
    context.network_icon = {};
    context.battery_icon_parts = {};
    clear_tray_indicator_icons(context.tray_indicator_icons);
    context.setup_status_label = nullptr;
    lv_obj_delete(context.root);
  }
  context.root = replacement;
  context.clock_label = context.staging_clock_label;
  context.network_icon = context.staging_network_icon;
  context.battery_icon_parts = context.staging_battery_icon;
  for (int i = 0; i < app_core::kMaxTrayIndicators; ++i) {
    context.tray_indicator_icons[i] = context.staging_tray_indicator_icons[i];
  }
  context.setup_status_label = context.staging_setup_status_label;
  context.staging_clock_label = nullptr;
  context.staging_network_icon = {};
  context.staging_battery_icon = {};
  clear_tray_indicator_icons(context.staging_tray_indicator_icons);
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

// Separate from update_visible_fields below, and called unconditionally on
// every ~100 ms tick rather than only when a publish or a clock-minute
// rollover happens to land on the same tick - see ui_app.cpp's
// timer_callback for the call site.
//
// That distinction is the actual bug a silent-audio hardware run traced back
// to here: this function's own comments already claimed the registry "reads
// fresh every tick regardless of whether a publish happened this cycle", but
// it used to live inside update_visible_fields(), which timer_callback only
// calls when (published_updated || clock_minute_changed) - true maybe twice
// a minute. A tone that starts and finishes inside the gap between those
// events set the registry's active flag correctly (confirmed: the sending
// side in modules/audio/audio.cpp was never the problem) and nothing on the
// receiving side ever polled it in time to notice, log it, or draw it - the
// exact "everything reports success and nothing happens" shape of a wire
// left unconnected, just one level removed from where it looked at first.
bool update_tray_indicators(UiContext& context) {
  // Logged unconditionally, even if context_ready() below is about to
  // return false - the whole point is to answer "did the value even get
  // this far" independently of whether there is currently anything to draw
  // it into.
  log_tray_indicator_state_changes();
  if (!context_ready(context)) return false;
  // render_tray() reserves every registered slot's cell on every page
  // unconditionally (see its own comment), so context.tray_indicator_icons[i]
  // is a real, already-drawn target on every tray-carrying page, not just the
  // ones that happened to be active when last rendered - a stale or wrong
  // icon was worse than one that simply never appeared, so this does not get
  // the indoor/weather/market wait-for-the-next-rebuild treatment.
  for (int i = 0; i < app_core::kMaxTrayIndicators; ++i) {
    const app_core::TrayIndicatorSlot slot = app_core::tray_indicator_slot(i);
    set_tray_indicator_icon_visible(context.tray_indicator_icons[i], slot.active);
  }
  return true;
}

bool update_visible_fields(UiContext& context,
                           const app_core::AppSnapshot& snapshot) {
  if (!context_ready(context)) return false;
  update_visible_clock(context, snapshot);
  // In place, like the label path it replaces: a battery sample every 30s must
  // not cost a page rebuild, which is a visible full repaint on this panel.
  set_wifi_icon_state(context.network_icon, snapshot.setup.connected);
  // See the initial-draw call site's own comment on valid vs. charging.
  set_battery_icon_level(
      context.battery_icon_parts, snapshot.battery.percent,
      snapshot.battery.valid,
      battery_is_charging(snapshot.battery, snapshot.battery_runtime.trend));
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
