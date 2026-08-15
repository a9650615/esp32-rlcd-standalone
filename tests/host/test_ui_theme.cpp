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
}
