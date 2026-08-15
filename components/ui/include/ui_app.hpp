#pragma once

#include "app_snapshot.hpp"
#include "ui_theme.hpp"

#ifndef UI_THEME_GEOMETRY_ONLY
#include <lvgl.h>

namespace ui {

void render_home(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds, uint8_t active_page);
void render_right_tiles(lv_obj_t* parent,
                        const app_core::AppSnapshot& snapshot, Rect bounds);
// Shared by data pages in the next UI slice; Home intentionally does not call
// this mast because its Clock Hero hierarchy is the page's primary content.
void render_mast(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                 Rect bounds);

// The caller owns the LVGL lock. `host` is a screen or stable container whose
// LVGL user-data slot is reserved by this API. A detached replacement is
// built completely before the previous owned page root is deleted and the
// replacement is made visible. Host deletion releases the owner state.
lv_obj_t* render_page(lv_obj_t* host, const app_core::AppSnapshot& snapshot,
                      app_core::PageId page,
                      Rect bounds = safe_canvas(),
                      uint8_t active_page = 0);

}  // namespace ui

#endif
