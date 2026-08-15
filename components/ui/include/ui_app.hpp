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

// The caller owns the LVGL lock. `host` is a screen or stable container. A
// hidden replacement is built completely before the previous page root is
// deleted and the replacement is made visible.
lv_obj_t* render_page(lv_obj_t* host, const app_core::AppSnapshot& snapshot,
                      app_core::PageId page,
                      Rect bounds = safe_canvas(),
                      uint8_t active_page = 0);

}  // namespace ui

#endif
