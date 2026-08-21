#define UI_THEME_GEOMETRY_ONLY
#include "ui_theme.hpp"
#include "ui_app.hpp"
#include "dither.hpp"

#include "test_support.hpp"

#include <cstdio>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<ui::UiContext>);
static_assert(!std::is_copy_assignable_v<ui::UiContext>);
static_assert(!std::is_move_constructible_v<ui::UiContext>);
static_assert(!std::is_move_assignable_v<ui::UiContext>);

HOST_TEST(ui_geometry_contract) {
  EXPECT_EQ(ui::kCanvasWidth, 400);
  EXPECT_EQ(ui::kCanvasHeight, 300);
  EXPECT_EQ(ui::kSafeMargin, 6);
  EXPECT_EQ(ui::kSeparatorWidth, 1);

  const ui::Rect canvas = ui::safe_canvas();
  EXPECT_EQ(canvas.x, 6);
  EXPECT_EQ(canvas.y, 6);
  EXPECT_EQ(canvas.right(), 394);
  EXPECT_EQ(canvas.bottom(), 294);
  EXPECT_TRUE(ui::within_safe_canvas(ui::Rect{6, 6, 388, 288}));
  EXPECT_TRUE(!ui::within_safe_canvas(ui::Rect{5, 6, 389, 288}));

  const auto cells = ui::right_tile_cells({0, 0, 118, 276});
  EXPECT_EQ(cells[0].height, 91);
  EXPECT_EQ(cells[1].y, cells[0].bottom() + ui::kSeparatorWidth);
  EXPECT_EQ(cells[2].bottom(), 276);
  EXPECT_TRUE(ui::tile_content_is_centered(cells[0]));
  EXPECT_TRUE(ui::tile_content_is_centered(cells[2]));
  // Home's single tall tile: centring must hold at full sidebar height too.
  // The retired footer check failed here purely because the cell is more than
  // three times its content tall, which is the layout working as designed.
  EXPECT_TRUE(ui::tile_content_is_centered(ui::Rect{0, 0, 118, 276}));

  ui::UiContext context;
  EXPECT_TRUE(!ui::context_ready(context));
}

HOST_TEST(rlcd_text_boxes_reserve_stroke_and_padding) {
  EXPECT_EQ(ui::kTextStrokeWidth, 1);
  EXPECT_EQ(ui::kTextInset, 1);
  EXPECT_EQ(ui::safe_text_box_height(16, 16), 18);
  EXPECT_EQ(ui::safe_text_box_height(20, 22), 24);
  EXPECT_EQ(ui::safe_text_box_height(59, 52), 59);
}

HOST_TEST(rlcd_outline_is_limited_to_small_text) {
  EXPECT_EQ(ui::text_outline_width(16), 1);
  EXPECT_EQ(ui::text_outline_width(22), 0);
  EXPECT_EQ(ui::text_outline_width(30), 0);
  EXPECT_EQ(ui::text_outline_width(52), 0);
}

HOST_TEST(right_tile_text_rows_do_not_overlap_and_fill_the_cell) {
  const ui::Rect cell{280, 28, 108, 91};
  const ui::TileTextLayout rows = ui::tile_text_layout(cell);

  EXPECT_TRUE(rows.title.y >= cell.y);
  EXPECT_TRUE(rows.title.bottom() <= rows.value.y);
  EXPECT_TRUE(rows.value.bottom() <= rows.detail.y);
  EXPECT_TRUE(rows.detail.bottom() <= cell.bottom());
  EXPECT_TRUE(ui::tile_content_is_centered(cell));
  const int top_gap = rows.title.y - cell.y;
  const int bottom_gap = cell.bottom() - rows.detail.bottom();
  EXPECT_TRUE(top_gap == bottom_gap || top_gap + 1 == bottom_gap ||
              top_gap == bottom_gap + 1);
}

HOST_TEST(right_tile_leading_visual_never_crosses_the_value_text) {
  const ui::Rect cell{280, 28, 108, 91};
  const ui::Rect market_visual = ui::tile_leading_visual_rect(cell, false);
  const ui::Rect market_value = ui::tile_value_rect(cell, false);
  EXPECT_EQ(market_visual.width, 0);
  EXPECT_EQ(market_value.x, ui::tile_text_layout(cell).value.x);

  const ui::Rect icon = ui::tile_leading_visual_rect(cell, true);
  const ui::Rect icon_value = ui::tile_value_rect(cell, true);
  EXPECT_TRUE(icon.right() <= icon_value.x);
  EXPECT_TRUE(icon_value.right() <= cell.right());
}

HOST_TEST(rlcd_data_lines_use_two_pixel_strokes) {
  EXPECT_EQ(ui::kDataLineWidth, 2);
}

// --- ui/include/dither.hpp -------------------------------------------

// The extremes never dither at all - level 0 is plain white, level 16 is
// plain solid - across every position in (and outside) one 4x4 tile, not
// just the origin.
HOST_TEST(dither_bayer4x4_extremes_are_uniform) {
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      EXPECT_TRUE(!ui::dither_bayer4x4_dark(x, y, 0));
      EXPECT_TRUE(ui::dither_bayer4x4_dark(x, y, 16));
    }
  }
}

// A mid level (8, "50% grey") paints exactly half of every 4x4 tile dark -
// the whole point of an ordered dither over a plain threshold, which would
// instead paint either all or none of a uniform input.
HOST_TEST(dither_bayer4x4_mid_level_is_exactly_half) {
  int dark_count = 0;
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      if (ui::dither_bayer4x4_dark(x, y, 8)) ++dark_count;
    }
  }
  EXPECT_EQ(dark_count, 8);
}

// Every level from 0 to 16 paints exactly that many of the 16 cells dark -
// the density ramp the debug test card relies on to show 0/12.5/25/50/75/
// 100% as visually distinct, evenly-stepped fills rather than six copies
// of whatever a rounding bug happens to produce.
HOST_TEST(dither_bayer4x4_every_level_matches_its_own_count) {
  for (int level = 0; level <= 16; ++level) {
    int dark_count = 0;
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        if (ui::dither_bayer4x4_dark(x, y, level)) ++dark_count;
      }
    }
    EXPECT_EQ(dark_count, level);
  }
}

// The pattern tiles seamlessly: the same level at the same position modulo
// 4 always agrees, arbitrarily far from the origin - a test card several
// tiles wide must not show a seam where one tile ends and the next begins.
HOST_TEST(dither_bayer4x4_tiles_seamlessly) {
  for (int level = 0; level <= 16; level += 4) {
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        const bool origin_tile = ui::dither_bayer4x4_dark(x, y, level);
        EXPECT_EQ(ui::dither_bayer4x4_dark(x + 40, y + 24, level),
                  origin_tile);
      }
    }
  }
}

// kMinDitherDimensionPx (16) is measured, not derived - see dither.hpp's own
// comment - but its consequence for dither_pixel_dark() is a plain
// boundary this can still lock down: at or above 16 on both dimensions,
// the real pattern; below 16 on either one, the flat fallback.
HOST_TEST(dither_pixel_dark_uses_the_real_pattern_at_the_measured_minimum) {
  EXPECT_EQ(ui::kMinDitherDimensionPx, 16);
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      EXPECT_EQ(ui::dither_pixel_dark(x, y, 16, 16, 8),
                ui::dither_bayer4x4_dark(x, y, 8));
    }
  }
}

// Below the minimum on either dimension - even if the other one is large -
// every pixel falls back to the same flat plain_threshold_dark() value,
// not the position-dependent pattern.
HOST_TEST(dither_pixel_dark_falls_back_below_the_minimum_on_either_dimension) {
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      EXPECT_EQ(ui::dither_pixel_dark(x, y, 15, 64, 8),
                ui::plain_threshold_dark(8));
      EXPECT_EQ(ui::dither_pixel_dark(x, y, 64, 12, 8),
                ui::plain_threshold_dark(8));
    }
  }
}

HOST_TEST(plain_threshold_dark_splits_exactly_at_the_midpoint) {
  EXPECT_TRUE(!ui::plain_threshold_dark(0));
  EXPECT_TRUE(!ui::plain_threshold_dark(7));
  EXPECT_TRUE(ui::plain_threshold_dark(8));
  EXPECT_TRUE(ui::plain_threshold_dark(16));
}

// The tray battery cell's real dimensions (kTrayBatteryIconWidth=30,
// kTrayBatteryIconHeight=14 in ui_data.hpp, not reachable from here - this header
// cannot depend on that one, so the numbers are repeated as literals, the
// same way battery_icon()'s own body_width/nub math already does).
constexpr ui::Rect kBatteryTrayCell{100, 3, 30, 14};

// Locks down the exact numbers battery_icon()'s comment claims: 2px inset
// from the top/left of the outline, body_width (bounds.width - nub - 1) - 4
// wide, bounds.height - 4 tall. A change to any of these is a change to
// where the charging bolt and the 100%-filled level bar both sit - this is
// what would need to change too, not something derived independently by
// each caller.
HOST_TEST(battery_fill_rect_matches_the_documented_inset_and_size) {
  const ui::Rect fill = ui::battery_fill_rect(kBatteryTrayCell);
  EXPECT_EQ(fill.x, kBatteryTrayCell.x + 2);
  EXPECT_EQ(fill.y, kBatteryTrayCell.y + 2);
  const int expected_body_width =
      kBatteryTrayCell.width - ui::kBatteryIconNubWidth - 1;
  EXPECT_EQ(fill.width, expected_body_width - 4);
  EXPECT_EQ(fill.height, kBatteryTrayCell.height - 4);
}

// The tray geometry assertion the charging bolt's "same footprint as the
// level variant" claim rests on: whatever battery_fill_rect() returns for
// the tray's real cell size stays entirely inside that cell. This is the
// build-time proof that a bolt bitmap sized and positioned from this
// function can never draw outside the icon's reserved space in the
// static_assert'ed tray layout (ui_data.hpp's system_tray_layout), not
// merely a claim that it does not currently.
HOST_TEST(battery_fill_rect_stays_within_the_tray_cell_it_is_computed_from) {
  const ui::Rect fill = ui::battery_fill_rect(kBatteryTrayCell);
  EXPECT_TRUE(ui::rect_within(kBatteryTrayCell, fill));
  // Both dimensions positive - a canvas with a zero or negative dimension is
  // exactly the kind of thing that would silently draw nothing rather than
  // fail loudly.
  EXPECT_TRUE(fill.width > 0);
  EXPECT_TRUE(fill.height > 0);
}

// --- build_battery_charging_composite() (ui_theme.hpp) -------------------
//
// This is the pure bit-pattern logic behind the tray battery's charging
// overlay: no LVGL, no ESP-IDF, a width, a height, a filled-column count in,
// a packed 1-bit buffer out. It was not tested here before, and the on-panel
// result was a completely empty outline for two implementations in a row -
// the same shape of miss as the tray indicator regression (host tests all
// green while the feature was dark on the panel), except that one genuinely
// could not be tested without LVGL and this one always could have been.

namespace {
bool bit_at(const uint8_t* buf, int stride, int x, int y) {
  return (buf[y * stride + x / 8] & (0x80 >> (x % 8))) != 0;
}
bool bolt_bit(int x, int y) { return ui::kChargingBoltRows[y][x] == 'X'; }
}  // namespace

// The real tray cell's fill rect happens to be exactly kChargingBoltWidth x
// kChargingBoltHeight (22x10 either way), so bolt_bit() above needs no
// centring offset to line up with it - if this ever stops being true, the
// tests below need the same offset arithmetic
// build_battery_charging_composite() itself uses, not a silent mismatch.
static_assert(ui::battery_fill_rect(kBatteryTrayCell).width ==
                  ui::kChargingBoltWidth,
              "test assumes the real tray cell's fill width equals the "
              "generated bolt's own width");
static_assert(ui::battery_fill_rect(kBatteryTrayCell).height ==
                  ui::kChargingBoltHeight,
              "test assumes the real tray cell's fill height equals the "
              "generated bolt's own height");

// The whole contract, now: a solid field with the bolt knocked out of it,
// unconditionally - no charge-boundary concept survives inside this
// function at all (see its own comment for the two designs that tried to
// have one, and why real hardware rejected both). `filled` was removed
// from the signature entirely rather than kept and ignored: there is no
// plausible future caller that would want to pass a charge level in here
// again without first re-deciding this whole design, at which point the
// signature would need to change back anyway, visibly, in the same diff -
// a compile error is a stronger guard against silently reinstating a
// level-dependent overlay than a runtime test asserting the parameter is
// ignored would have been.
HOST_TEST(battery_charging_composite_knocks_the_bolt_out_of_a_solid_field) {
  const int width = ui::kChargingBoltWidth;
  const int height = ui::kChargingBoltHeight;
  const int stride = (width + 7) / 8;
  uint8_t buf[64] = {};
  ui::build_battery_charging_composite(buf, sizeof(buf), width, height, stride);

  bool any_ink = false;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(bit_at(buf, stride, x, y), !bolt_bit(x, y));
      any_ink = any_ink || bit_at(buf, stride, x, y);
    }
  }
  EXPECT_TRUE(any_ink);

  // The shape the code actually produced, not the shape kChargingBoltRows
  // contains - printed unconditionally so it is visible in the ordinary
  // test run, not just on failure.
  std::printf("battery charging composite (%dx%d):\n", width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      std::putchar(bit_at(buf, stride, x, y) ? 'X' : '.');
    }
    std::putchar('\n');
  }
}

// The stride regression: a version of this function once computed its own
// row pitch as a bare (width + 7) / 8 - the tightest possible pack - and
// the panel showed progressive diagonal smearing, because LVGL pads each
// canvas row to its own stride (lv_draw_buf_width_to_stride(), which the
// firmware call site now supplies explicitly rather than this function
// guessing at it). A test that only ever exercised the tight pack would
// keep passing while that bug shipped - which is exactly what happened -
// so this deliberately asks for one byte more per row than the tight pack
// needs and checks that every pixel still lands where it should, not one
// row short of where the tight-pack test above would have placed it. Still
// valuable after `filled` was dropped - orthogonal concern, same buffer.
HOST_TEST(battery_charging_composite_honours_a_stride_wider_than_the_tight_pack) {
  const int width = ui::kChargingBoltWidth;
  const int height = ui::kChargingBoltHeight;
  const int tight_stride = (width + 7) / 8;
  const int padded_stride = tight_stride + 1;
  uint8_t buf[64] = {};
  ui::build_battery_charging_composite(buf, sizeof(buf), width, height,
                                       padded_stride);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(bit_at(buf, padded_stride, x, y), !bolt_bit(x, y));
    }
    // The padding byte itself must stay untouched, not overrun by a row
    // that assumed the tight pack instead of the stride it was actually
    // given - a one-byte "smear" would still pass the pixel checks above
    // if this row's padding silently became the next row's first byte.
    EXPECT_EQ(buf[y * padded_stride + tight_stride], 0);
  }
}

// --- repack_i1_bits() (ui_theme.hpp) --------------------------------------
//
// The shared repacking step behind tray_indicator_icon()'s per-slot
// storage: takes a module's tight-packed bits (the format
// app_core::TrayIndicatorBitmap documents) and copies them to a given
// palette offset and stride, neither of which is guaranteed to match the
// tight pack - the same lesson build_battery_charging_composite() above
// just re-learned, shared here so a fourth icon does not have to relearn
// it again.
HOST_TEST(repack_i1_bits_places_every_row_at_the_given_offset_and_stride) {
  // A tiny 10x3 source, tight-packed: stride (10+7)/8 = 2 bytes/row.
  const int width = 10;
  const int height = 3;
  const int tight_stride = (width + 7) / 8;
  const uint8_t tight[] = {
      0b10110000, 0b11000000,  // row 0
      0b00001111, 0b00000000,  // row 1
      0b11111111, 0b11000000,  // row 2
  };

  const int palette_bytes = 8;
  const int stride = tight_stride + 1;  // deliberately wider than tight_stride
  uint8_t out[64] = {};
  ui::repack_i1_bits(tight, out, sizeof(out), width, height, stride,
                     palette_bytes);

  auto src_bit = [&](int x, int y) {
    return (tight[y * tight_stride + x / 8] & (0x80 >> (x % 8))) != 0;
  };
  auto dst_bit = [&](int x, int y) {
    return (out[palette_bytes + y * stride + x / 8] & (0x80 >> (x % 8))) != 0;
  };
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(dst_bit(x, y), src_bit(x, y));
    }
  }
  // The palette region is not this function's to touch - that belongs to
  // lv_canvas_set_palette(), called separately once the canvas exists.
  for (int i = 0; i < palette_bytes; ++i) {
    EXPECT_EQ(out[i], 0);
  }
}

HOST_TEST(repack_i1_bits_does_nothing_if_the_destination_is_too_small) {
  const uint8_t tight[2] = {0xFF, 0xFF};
  uint8_t out[4] = {1, 2, 3, 4};
  // 8 (palette) + 2 (stride) * 3 (height) = 14 > 4 capacity - must refuse
  // rather than overrun, the same guard build_battery_charging_composite()
  // has.
  ui::repack_i1_bits(tight, out, sizeof(out), /*width=*/10, /*height=*/3,
                     /*stride=*/2, /*palette_bytes=*/8);
  EXPECT_EQ(out[0], 1);
  EXPECT_EQ(out[1], 2);
  EXPECT_EQ(out[2], 3);
  EXPECT_EQ(out[3], 4);
}
