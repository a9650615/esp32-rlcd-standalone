#define UI_THEME_GEOMETRY_ONLY
#include "ui_theme.hpp"
#include "ui_app.hpp"
#include "dither.hpp"

#include "test_support.hpp"

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
