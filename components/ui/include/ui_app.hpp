#pragma once

#include "app_snapshot.hpp"
#include "ui_theme.hpp"

#ifndef UI_THEME_GEOMETRY_ONLY
#include <lvgl.h>
#endif

namespace ui {

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
                 Rect bounds, uint8_t active_page);
void render_right_tiles(lv_obj_t* parent,
                        const app_core::AppSnapshot& snapshot, Rect bounds);
// Shared by data pages in the next UI slice; Home intentionally does not call
// this mast because its Clock Hero hierarchy is the page's primary content.
void render_mast(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds);

// The caller owns the LVGL lock. A detached replacement is built completely
// before the previous context-owned page root is deleted and the replacement
// is made visible. Context state is caller-owned; host deletion invalidates it.
lv_obj_t* render_page(UiContext& context,
                      const app_core::AppSnapshot& snapshot,
                      app_core::PageId page,
                      Rect bounds = safe_canvas(),
                      uint8_t active_page = 0);

lv_obj_t* navigation_overlay(UiContext& context, Rect bounds);

#endif

}  // namespace ui
