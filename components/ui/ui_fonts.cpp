#include "ui_fonts.hpp"

LV_FONT_DECLARE(rlcd_cjk_14)
LV_FONT_DECLARE(rlcd_cjk_20)
LV_FONT_DECLARE(rlcd_cjk_28)
LV_FONT_DECLARE(rlcd_digits_128)

namespace ui {
namespace {

// Copies rather than references: LVGL's built-in faces are const, so the
// fallback pointer cannot be attached to them in place.
lv_font_t g_small;
lv_font_t g_medium;
lv_font_t g_large;
bool g_ready = false;

}  // namespace

void fonts_init() {
  if (g_ready) return;
  g_small = lv_font_montserrat_14;
  g_small.fallback = &rlcd_cjk_14;
  g_medium = lv_font_montserrat_20;
  g_medium.fallback = &rlcd_cjk_20;
  g_large = lv_font_montserrat_28;
  g_large.fallback = &rlcd_cjk_28;
  g_ready = true;
}

// Each falls back to plain Montserrat before fonts_init(), so an early caller
// renders Latin correctly instead of dereferencing a zeroed lv_font_t.
const lv_font_t* font_small() {
  return g_ready ? &g_small : &lv_font_montserrat_14;
}
const lv_font_t* font_medium() {
  return g_ready ? &g_medium : &lv_font_montserrat_20;
}
const lv_font_t* font_large() {
  return g_ready ? &g_large : &lv_font_montserrat_28;
}
const lv_font_t* font_hero() { return &rlcd_digits_128; }

}  // namespace ui
