#define UI_THEME_GEOMETRY_ONLY
#include "ui_theme.hpp"
#include "ui_app.hpp"

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
  EXPECT_TRUE(ui::tile_content_has_no_reserved_footer(cells[2]));

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
