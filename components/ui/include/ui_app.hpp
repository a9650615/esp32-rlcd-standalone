#pragma once

#include "app_snapshot.hpp"
#include "page_registry.hpp"
#include "ui_data.hpp"
#include "ui_theme.hpp"

#include <cstddef>

#ifndef UI_THEME_GEOMETRY_ONLY
#include <lvgl.h>
#endif

namespace ui {

// Starts the UI lifecycle. The caller owns startup ordering; this function
// only schedules the LVGL-thread runtime and retains a copy of the snapshot.
// rtc_fallback is diagnostic state and does not alter the read-only RTC data.
bool start(const app_core::AppSnapshot& snapshot,
           const app_core::RtcDateTime& clock, bool rtc_fallback);

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
                 std::size_t page_count);
void render_right_tiles(lv_obj_t* parent,
                        const app_core::AppSnapshot& snapshot, Rect bounds);
void render_market_sidebar(lv_obj_t* parent,
                           const app_core::AppSnapshot& snapshot,
                           const app_core::MarketData& market,
                           Rect bounds, bool us_market);
// Shared by data pages in the next UI slice; Home intentionally does not call
// this mast because its Clock Hero hierarchy is the page's primary content.
void render_mast(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds);
void render_market(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                   const app_core::MarketData& market, Rect bounds,
                   std::size_t page_index, std::size_t page_count,
                   bool us_market);
void render_weather(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                    Rect bounds, std::size_t page_index,
                    std::size_t page_count);
void render_indoor(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                   Rect bounds, std::size_t page_index,
                   std::size_t page_count);

// The caller owns the LVGL lock. A detached replacement is built completely
// before the previous context-owned page root is deleted and the replacement
// is made visible. Context state is caller-owned; host deletion invalidates it.
lv_obj_t* render_page(UiContext& context,
                      const app_core::AppSnapshot& snapshot,
                      app_core::PageId page, Rect bounds,
                      std::size_t page_index, std::size_t page_count);

lv_obj_t* navigation_overlay(UiContext& context, Rect bounds);

#endif

}  // namespace ui
