#pragma once

#include "app_snapshot.hpp"
#include "page_registry.hpp"
#include "ui_data.hpp"
#include "settings_menu.hpp"
#include "ui_strings.hpp"
#include "ui_theme.hpp"

#include <cstddef>
#include <string>

#ifndef UI_THEME_GEOMETRY_ONLY
#include <lvgl.h>

// Pulls in lvgl.h itself, so it stays inside the guard host tests rely on.
#include "ui_fonts.hpp"
#endif

namespace ui {

// Starts the UI lifecycle. The caller owns startup ordering; this function
// only schedules the LVGL-thread runtime and retains a copy of the snapshot.
// rtc_fallback is diagnostic state and does not alter the read-only RTC data.
bool start(const app_core::AppSnapshot& snapshot,
           const app_core::RtcDateTime& clock, bool rtc_fallback);

// Callable from any FreeRTOS task. Stores a copy under a mutex and makes no
// lv_* calls itself; the existing LVGL-thread timer applies it (rendering
// the Setup page while snapshot.setup.active is true, otherwise leaving the
// carousel alone).
void publish_snapshot(const app_core::AppSnapshot& snapshot);

// Invoked on the LVGL thread when board::ButtonEvent::EnterSetup arrives.
// Pass nullptr to unregister; the button drain is null-safe either way.
void set_setup_gesture_handler(void (*handler)());
// Registers what the settings menu's "check for updates" row runs. The handler
// is expected to return immediately and do the work on its own task, then
// report through set_update_status.
void set_update_check_handler(void (*handler)());
// Callable from any task. The text lands on the settings page's update row on
// the next LVGL tick.
void set_update_status(const std::string& status);

// Address-sensitive owner retained by the caller for the host's entire LVGL
// lifetime. LVGL stores a pointer to this exact instance in the host delete
// callback; initialize/reset it in place and never copy or move it.
struct UiContext {
  UiContext() = default;
  UiContext(const UiContext&) = delete;
  UiContext& operator=(const UiContext&) = delete;
  UiContext(UiContext&&) = delete;
  UiContext& operator=(UiContext&&) = delete;

  lv_obj_t* host = nullptr;
  lv_obj_t* root = nullptr;
  // Owned by the current root; cleared before that root is deleted. Each is
  // non-null only while the page that renders it is the one currently on
  // screen (e.g. network_label/battery_label only while a tray page is
  // showing; setup_status_label only while Setup is showing), so
  // update_visible_fields can unconditionally attempt every field and rely
  // on the null checks to scope the update to what's actually visible.
  // Which settings row the cursor is on, and the result of the last update
  // check. Held here rather than in the renderer so a redraw - a language
  // change repaints every row - does not lose the cursor.
  std::size_t settings_focus = 0;
  std::string settings_status;

  lv_obj_t* clock_label = nullptr;
  lv_obj_t* network_label = nullptr;
  lv_obj_t* battery_label = nullptr;
  lv_obj_t* setup_status_label = nullptr;
  // Staging-only registrations used during atomic replacement.
  lv_obj_t* staging_clock_label = nullptr;
  lv_obj_t* staging_network_label = nullptr;
  lv_obj_t* staging_battery_label = nullptr;
  lv_obj_t* staging_setup_status_label = nullptr;
  bool initialized = false;
};

constexpr bool context_ready(const UiContext& context) {
  return context.initialized && context.host != nullptr;
}

#ifndef UI_THEME_GEOMETRY_ONLY

// The caller retains this context for at least as long as the host. Host
// deletion invalidates both pointers through the registered LVGL delete hook.
bool init_context(UiContext& context, lv_obj_t* host);
void reset_context(UiContext& context);

void render_home(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds, std::size_t page_index,
                 std::size_t page_count, UiContext* context = nullptr);
void render_right_tiles(lv_obj_t* parent,
                        const app_core::AppSnapshot& snapshot, Rect bounds);
void render_market_sidebar(lv_obj_t* parent,
                           const app_core::AppSnapshot& snapshot,
                           const app_core::MarketData& market,
                           Rect bounds, bool us_market);
// Persistent system tray: time, network status, page position, battery
// flush right. Rendered once per page from render_page (render_shared.cpp)
// for every page where page_shows_tray() is true. Home opts out so its
// Clock Hero keeps the full canvas instead - see the render_home.cpp doc
// comment for what Home does about page position instead. page_index/
// page_count are passed straight through to the tray's own page_dots cell;
// Setup calls this with (0, 0), which draws no dots at all.
void render_tray(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds, std::size_t page_index, std::size_t page_count,
                 UiContext* context = nullptr);
void render_market(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                   const app_core::MarketData& market, Rect bounds,
                   std::size_t page_index, std::size_t page_count,
                   bool us_market, UiContext* context = nullptr);
void render_weather(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                    Rect bounds, std::size_t page_index,
                    std::size_t page_count, UiContext* context = nullptr);
void render_indoor(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                   Rect bounds, std::size_t page_index,
                   std::size_t page_count, UiContext* context = nullptr);
// Additive page, not part of the five-page carousel; page_index/page_count
// are unused (no page_dots). Shown only while snapshot.setup.active is true.
void render_setup(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                  Rect bounds, std::size_t page_index,
                  std::size_t page_count, UiContext* context = nullptr);
// Additive page like Setup, and it outranks Setup: while it is up the
// firmware is writing its own flash, so nothing may navigate away from it.
void render_ota(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                Rect bounds, std::size_t page_index, std::size_t page_count,
                UiContext* context = nullptr);
// Additive page, entered by a BOOT long press. Reads its cursor position and
// last-check status from `context`.
void render_settings(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                     Rect bounds, std::size_t page_index,
                     std::size_t page_count, UiContext* context = nullptr);

// The caller owns the LVGL lock. A detached replacement is built completely
// before the previous context-owned page root is deleted and the replacement
// is made visible. Context state is caller-owned; host deletion invalidates it.
lv_obj_t* render_page(UiContext& context,
                      const app_core::AppSnapshot& snapshot,
                      app_core::PageId page, Rect bounds,
                      std::size_t page_index, std::size_t page_count);

// Caller owns the LVGL lock. Only the currently visible clock label is
// mutated; page roots are never rebuilt for a minute refresh.
bool update_visible_clock(UiContext& context,
                          const app_core::AppSnapshot& snapshot);

// Caller owns the LVGL lock. The repaint-throttling entry point for every
// snapshot field that can change without a page-identity change: tray
// time/network/battery plus the Setup status line. Extends the same
// registered-label-pointer + compare-before-set mechanism update_visible_clock
// uses (see render_shared.cpp) to the rest of the tray and Setup status;
// fields whose label isn't currently on screen are simply null and skipped.
// No page root is ever touched here.
bool update_visible_fields(UiContext& context,
                           const app_core::AppSnapshot& snapshot);

lv_obj_t* navigation_overlay(UiContext& context, Rect bounds);

#endif

}  // namespace ui
