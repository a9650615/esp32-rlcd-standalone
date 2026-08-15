#define UI_THEME_GEOMETRY_ONLY
#include "ui_data.hpp"

#include "test_support.hpp"

#include <array>

HOST_TEST(chart_points_are_normalized_and_clamped_to_bounds) {
  const ui::Rect chart{100, 32, 260, 180};
  const auto points = ui::normalize_chart_samples(
      std::array<int, 8>{-100, 10, 10, 100, 1'000, 20, 40, 80}, chart);

  EXPECT_EQ(points.front().x, chart.x);
  EXPECT_EQ(points.back().x, chart.right() - 1);
  for (const auto& point : points) {
    EXPECT_TRUE(point.x >= chart.x && point.x < chart.right());
    EXPECT_TRUE(point.y >= chart.y && point.y < chart.bottom());
  }
  EXPECT_EQ(points.front().y, chart.bottom() - 1);
  EXPECT_EQ(points[4].y, chart.y);
}

HOST_TEST(chart_constant_samples_use_a_stable_center_line) {
  const ui::Rect chart{10, 20, 80, 40};
  const auto points = ui::normalize_chart_samples(
      std::array<int, 8>{7, 7, 7, 7, 7, 7, 7, 7}, chart);
  for (const auto& point : points) {
    EXPECT_EQ(point.y, chart.y + chart.height / 2);
  }
}

HOST_TEST(market_layout_keeps_primary_area_at_seventy_two_percent) {
  const auto layout = ui::market_layout(ui::Rect{6, 6, 388, 288});
  EXPECT_EQ(layout.primary.width, 279);
  EXPECT_EQ(layout.side.x, layout.primary.right() + ui::kSeparatorWidth);
  EXPECT_EQ(layout.side.right(), 394);
  EXPECT_EQ(layout.side.bottom(), 294);
}

HOST_TEST(forecast_columns_are_equal_and_fill_the_available_width) {
  const ui::Rect forecast{80, 80, 280, 120};
  const auto columns = ui::forecast_columns(forecast);
  EXPECT_EQ(columns.front().x, forecast.x);
  EXPECT_EQ(columns.back().right(), forecast.right());
  for (std::size_t index = 1; index < columns.size(); ++index) {
    EXPECT_EQ(columns[index].width, columns.front().width);
    EXPECT_EQ(columns[index].x, columns[index - 1].right());
  }
}
