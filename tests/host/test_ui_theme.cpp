#define UI_THEME_GEOMETRY_ONLY
#include "ui_theme.hpp"

#include "test_support.hpp"

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
}
