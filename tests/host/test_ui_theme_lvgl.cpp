// Regression coverage for bind_i1_canvas()'s background-opacity/-colour
// parameters (components/ui/ui_theme.cpp). This is a separate binary from
// host_tests: it is the one place in the host suite that links real LVGL
// (see UI_THEME_LVGL_TESTS in CMakeLists.txt for why the rest of the suite
// deliberately does not), because the bug this file exists to catch - the
// parameter being silently ignored - can only be observed by reading a real
// lv_obj_t's own style back, not by inspecting bind_i1_canvas()'s inputs.
#include "ui_theme.hpp"
#include "test_support.hpp"

#include <lvgl.h>

#include <cstdint>
#include <cstdio>
#include <iostream>

namespace {

// One shared display for every test in this binary - lv_obj_class_create_obj
// refuses to create a screen without one (see lv_obj_class.c), and nothing
// here ever calls lv_timer_handler()/lv_refr_now(), so it never needs real
// draw buffers.
lv_display_t* test_display() {
  static lv_display_t* display = [] {
    lv_init();
    return lv_display_create(64, 64);
  }();
  return display;
}

lv_obj_t* fresh_parent() {
  // A fresh child object per test, not the screen itself: apply_surface()
  // touches the *object passed to bind_i1_canvas*, not its parent, but
  // keeping each test's canvas under its own throwaway parent means nothing
  // it does can leak into another test via the screen's own children list.
  lv_obj_t* parent = lv_obj_create(lv_display_get_screen_active(test_display()));
  return parent;
}

}  // namespace

// The bug: bind_i1_canvas() took `background`/`background_opa` and never
// applied either to the canvas object's own style, leaving apply_surface()'s
// unconditional opaque white. Proven here the way the bug actually manifests
// - by reading the canvas's own LV_PART_MAIN style back - rather than by
// inspecting what bind_i1_canvas() was merely passed.
HOST_TEST(bind_i1_canvas_honours_transparent_background) {
  lv_obj_t* parent = fresh_parent();
  uint8_t storage[ui::i1_canvas_pixel_offset() + 8 * 4];
  lv_obj_t* canvas = ui::bind_i1_canvas(parent, 0, 0, 4, 4, storage,
                                        lv_color_white(), LV_OPA_TRANSP,
                                        lv_color_black());
  EXPECT_TRUE(canvas != nullptr);
  EXPECT_EQ(lv_obj_get_style_bg_opa(canvas, LV_PART_MAIN),
            static_cast<lv_opa_t>(LV_OPA_TRANSP));
}

HOST_TEST(bind_i1_canvas_honours_opaque_background) {
  lv_obj_t* parent = fresh_parent();
  uint8_t storage[ui::i1_canvas_pixel_offset() + 8 * 4];
  lv_obj_t* canvas = ui::bind_i1_canvas(parent, 0, 0, 4, 4, storage,
                                        lv_color_white(), LV_OPA_COVER,
                                        lv_color_black());
  EXPECT_TRUE(canvas != nullptr);
  EXPECT_EQ(lv_obj_get_style_bg_opa(canvas, LV_PART_MAIN),
            static_cast<lv_opa_t>(LV_OPA_COVER));
}

// Not just "opa forwarded" - the colour argument too, and with a value that
// could not be confused with apply_surface()'s own white default.
HOST_TEST(bind_i1_canvas_honours_background_colour) {
  lv_obj_t* parent = fresh_parent();
  uint8_t storage[ui::i1_canvas_pixel_offset() + 8 * 4];
  const lv_color_t red = lv_color_make(0xff, 0x00, 0x00);
  lv_obj_t* canvas =
      ui::bind_i1_canvas(parent, 0, 0, 4, 4, storage, red, LV_OPA_COVER,
                          lv_color_black());
  EXPECT_TRUE(canvas != nullptr);
  const lv_color_t applied = lv_obj_get_style_bg_color(canvas, LV_PART_MAIN);
  EXPECT_TRUE(applied.red == red.red && applied.green == red.green &&
              applied.blue == red.blue);
}

// battery_icon()'s charging overlay: must stay opaque white. This is the
// regression the gaps doc warned about - honouring the parameter must not
// mean defaulting the canvas to transparent.
HOST_TEST(battery_icon_charging_overlay_stays_opaque_white) {
  lv_obj_t* parent = fresh_parent();
  ui::BatteryIconParts parts =
      ui::battery_icon(parent, {0, 0, 24, 12}, 50, true, true);
  EXPECT_TRUE(parts.charging_bolt != nullptr);
  EXPECT_EQ(lv_obj_get_style_bg_opa(parts.charging_bolt, LV_PART_MAIN),
            static_cast<lv_opa_t>(LV_OPA_COVER));
  const lv_color_t bg =
      lv_obj_get_style_bg_color(parts.charging_bolt, LV_PART_MAIN);
  const lv_color_t white = lv_color_white();
  EXPECT_TRUE(bg.red == white.red && bg.green == white.green &&
              bg.blue == white.blue);
}

// tray_indicator_icon(): must stay transparent, so the tray's own background
// shows through rather than a redundant white square behind every module's
// icon.
HOST_TEST(tray_indicator_icon_stays_transparent) {
  lv_obj_t* parent = fresh_parent();
  static const uint8_t bits[2] = {0xff, 0xff};
  app_core::TrayIndicatorBitmap bitmap{bits, 8, 2};
  ui::TrayIndicatorIcon icon =
      ui::tray_indicator_icon(parent, {0, 0, 8, 2}, 0, bitmap);
  EXPECT_TRUE(icon.canvas != nullptr);
  EXPECT_EQ(lv_obj_get_style_bg_opa(icon.canvas, LV_PART_MAIN),
            static_cast<lv_opa_t>(LV_OPA_TRANSP));
}
