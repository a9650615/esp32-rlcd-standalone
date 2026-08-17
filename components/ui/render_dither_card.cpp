// Debug-only (#ifndef NDEBUG, see ui_app.hpp's declaration of
// build_dither_card_screen()): a static test card demonstrating
// dither.hpp's 4x4 ordered-dither pattern on the actual panel, and nothing
// else - it does not read a real asset, does not touch
// board_rlcd/lvgl_port.cpp's flush_display(), and is not part of the
// carousel, the tray, or app_core::PageId. See modules/audio/README.md's
// "Verifying the tray's speaker indicator" section for the same kind of
// "what a screenshot proves that a host test cannot" honesty this needs
// too: every host test that could cover this covers dither.hpp's own
// pattern (tests/host/test_ui_theme.cpp), not what it looks like rendered -
// that is exactly what GET /dither-card + GET /shot is for.
#ifndef NDEBUG

#include "ui_app.hpp"
#include "ui_fonts.hpp"
#include "dither.hpp"

namespace ui {
namespace {

constexpr int kSwatchCount = 6;

// 0/12.5/25/50/75/100% as sixteenths (dither_bayer4x4_dark's own scale) -
// the exact set of fill levels a 4x4 matrix can hit exactly, so the ramp
// shows six genuinely distinct, evenly-stepped densities rather than
// rounding several of them to the same visual result.
constexpr int kDensityLevels[kSwatchCount] = {0, 2, 4, 8, 12, 16};
constexpr const char* kDensityLabels[kSwatchCount] = {"0%", "12.5%", "25%",
                                                      "50%", "75%", "100%"};

// Same 50% grey (level 8) throughout - only the square's own size changes,
// to give the operator something to judge "how small can a dithered patch
// get before it stops looking like grey" against. Deliberately no
// minimum-size constant anywhere in this codebase yet: that judgment call
// is exactly what this row exists to let the operator make with their own
// eyes, not a number to guess at here.
constexpr int kSizeRampSizes[kSwatchCount] = {64, 48, 32, 24, 16, 12};
constexpr int kSizeRampLevel = 8;
constexpr const char* kSizeRampLabels[kSwatchCount] = {
    "64px", "48px", "32px", "24px", "16px", "12px"};

constexpr int kMaxSwatchSize = 64;
// This used to be its own tight (width+7)/8 ceiling-divide, bound to
// lv_canvas_set_buffer() at byte 0 - the same pattern ui::tray_indicator_icon()
// (ui_theme.cpp) had until it was found to render nothing/smeared on real
// hardware: LVGL reserves palette bytes at the buffer's front and pads rows
// to its own stride, neither of which that formula accounted for. Sized via
// ui::i1_canvas_storage_bytes() now, the same call every other I1 canvas in
// this codebase sizes its backing store with, rather than a second
// hand-derived formula that could drift from LVGL's own the same way the
// original one did. One static backing array per swatch, sized for the
// largest size ramp entry and reused as-is (smaller swatches simply use
// fewer of their own bytes).
uint8_t g_density_bitmaps[kSwatchCount][i1_canvas_pixel_offset() +
                                       kMaxSwatchSize * kMaxSwatchSize];
uint8_t g_size_ramp_bitmaps[kSwatchCount][i1_canvas_pixel_offset() +
                                         kMaxSwatchSize * kMaxSwatchSize];

void set_bit(uint8_t* buffer, int stride, int x, int y) {
  buffer[y * stride + x / 8] |= static_cast<uint8_t>(0x80 >> (x & 7));
}

// Fills a size x size 1bpp bitmap into `buffer`, starting at
// i1_canvas_pixel_offset() (LVGL's own palette lives in the bytes before
// that - see i1_canvas_pixel_offset()'s own comment) and laid out at
// i1_canvas_stride(size)'s own row pitch, not a tight (width+7)/8 pack -
// the same two rules ui::repack_i1_bits() and
// ui::build_battery_charging_composite() already follow, asked from LVGL
// directly here for the same reason their own comments give: a bare
// (width+7)/8 only agrees with LVGL's real stride when the project's row-
// padding config happens to be 1, and silently drifts the moment it isn't.
void build_swatch_bitmap(uint8_t* buffer, int size, int level) {
  uint8_t* pixels = buffer + i1_canvas_pixel_offset();
  const int stride = i1_canvas_stride(size);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      if (dither_bayer4x4_dark(x, y, level)) set_bit(pixels, stride, x, y);
    }
  }
}

lv_obj_t* swatch_canvas(lv_obj_t* parent, uint8_t* buffer, int size, Rect bounds) {
  // Opaque both ways, unlike tray_indicator_icon()'s transparent "off" -
  // this is a standalone full-screen card, not an overlay on other content.
  return bind_i1_canvas(parent, bounds.x, bounds.y, size, size, buffer,
                        lv_color_white(), LV_OPA_COVER, lv_color_black());
}

// One row of swatches, evenly spread across `safe`'s width, with a caption
// label centered under each. Returns the y just below the captions, so the
// caller can stack the next row under it.
int build_ramp_row(lv_obj_t* screen, const Rect& safe, int row_y,
                   uint8_t (*bitmaps)[i1_canvas_pixel_offset() +
                                      kMaxSwatchSize * kMaxSwatchSize],
                   const int* sizes, const int* levels, const char* const* labels,
                   int row_height) {
  const int cell_width = safe.width / kSwatchCount;
  const int leftover = safe.width - cell_width * kSwatchCount;
  const int start_x = safe.x + leftover / 2;
  const int row_bottom = row_y + row_height;

  for (int i = 0; i < kSwatchCount; ++i) {
    const int size = sizes[i];
    build_swatch_bitmap(bitmaps[i], size, levels[i]);
    const int column_x = start_x + i * cell_width;
    swatch_canvas(screen, bitmaps[i], size,
                  {column_x + (cell_width - size) / 2, row_bottom - size, size, size});
    label(screen, labels[i], {column_x, row_bottom + 2, cell_width, 14}, font_small(),
          LV_TEXT_ALIGN_CENTER, false);
  }
  return row_bottom + 2 + 14;
}

}  // namespace

lv_obj_t* build_dither_card_screen() {
  lv_obj_t* screen = lv_obj_create(nullptr);
  if (screen == nullptr) return nullptr;
  apply_surface(screen);
  lv_obj_set_size(screen, kCanvasWidth, kCanvasHeight);

  const Rect safe = safe_canvas();
  int y = safe.y;
  label(screen, "Dither test card (4x4 Bayer, ordered)", {safe.x, y, safe.width, 18},
        font_small(), LV_TEXT_ALIGN_CENTER, false);
  y += 18 + 4;

  label(screen, "Density ramp", {safe.x, y, safe.width, 14}, font_small(),
        LV_TEXT_ALIGN_CENTER, false);
  y += 14 + 4;
  int sizes_all_48[kSwatchCount];
  for (int& size : sizes_all_48) size = 48;
  y = build_ramp_row(screen, safe, y, g_density_bitmaps, sizes_all_48, kDensityLevels,
                     kDensityLabels, 48);
  y += 10;

  label(screen, "50% grey, size ramp", {safe.x, y, safe.width, 14}, font_small(),
        LV_TEXT_ALIGN_CENTER, false);
  y += 14 + 4;
  int levels_all_8[kSwatchCount];
  for (int& level : levels_all_8) level = kSizeRampLevel;
  y = build_ramp_row(screen, safe, y, g_size_ramp_bitmaps, kSizeRampSizes, levels_all_8,
                     kSizeRampLabels, kMaxSwatchSize);
  y += 6;

  label(screen, "Static test card - not a real asset", {safe.x, y, safe.width, 14},
        font_small(), LV_TEXT_ALIGN_CENTER, false);

  return screen;
}

}  // namespace ui

#endif  // NDEBUG
