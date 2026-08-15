#define UI_THEME_GEOMETRY_ONLY
#include "app_snapshot.hpp"
#include "ui_data.hpp"

#include "test_support.hpp"

#include <array>

namespace {

bool is_printable_ascii(const char* text) {
  for (const unsigned char* cursor =
           reinterpret_cast<const unsigned char*>(text);
       *cursor != '\0'; ++cursor) {
    if (*cursor < 0x20 || *cursor > 0x7e) return false;
  }
  return true;
}

}  // namespace

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

HOST_TEST(page_dots_geometry_supports_five_four_and_zero_pages) {
  const ui::Rect bounds{6, 6, 388, 288};
  const auto five = ui::page_dots_geometry(bounds, 3, 5);
  EXPECT_EQ(five.count, static_cast<std::size_t>(5));
  EXPECT_EQ(five.active_index, static_cast<std::size_t>(3));
  EXPECT_EQ(five.total_width, 41);
  EXPECT_EQ(five.start_x, bounds.right() - 41);

  const auto four = ui::page_dots_geometry(bounds, 9, 4);
  EXPECT_EQ(four.count, static_cast<std::size_t>(4));
  EXPECT_EQ(four.active_index, static_cast<std::size_t>(3));
  EXPECT_EQ(four.total_width, 32);

  const auto none = ui::page_dots_geometry(bounds, 0, 0);
  EXPECT_EQ(none.count, static_cast<std::size_t>(0));
  EXPECT_EQ(none.active_index, static_cast<std::size_t>(0));
  EXPECT_EQ(none.total_width, 0);
}

HOST_TEST(minute_formatter_keeps_only_hours_and_minutes) {
  EXPECT_EQ(ui::format_minute_clock("09:41:59"), std::string("09:41"));
  EXPECT_EQ(ui::format_minute_clock("09:41"), std::string("09:41"));
  EXPECT_EQ(ui::format_minute_clock("unknown"), std::string("unknown"));
}

HOST_TEST(mast_clock_source_is_compact_and_truthful) {
  // The DEMO prefix used to be on every case, including ones that are not
  // demo data at all. Network-synced time is real, RTC time is real, and only
  // the compile-time fallback is fabricated - labelling all three the same way
  // was itself untruthful.
  EXPECT_EQ(ui::compact_clock_source("SNTP"), std::string("SYNC"));
  EXPECT_EQ(ui::compact_clock_source("PCF85063"), std::string("RTC"));
  EXPECT_EQ(ui::compact_clock_source("RTC fallback"), std::string("FALLBACK"));
  EXPECT_EQ(ui::compact_clock_source(""), std::string("UNKNOWN"));
}

HOST_TEST(comfort_band_label_uses_supported_ascii_glyphs) {
  EXPECT_TRUE(is_printable_ascii(ui::kComfortBandLabel));
  EXPECT_EQ(std::string(ui::kComfortBandLabel),
            std::string("COMFORT BAND  40-60 RH"));
}

HOST_TEST(new_york_fixture_is_distinct_from_taipei_weather) {
  const app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  EXPECT_EQ(snapshot.weather.current.location, std::string("Taipei"));
  EXPECT_EQ(snapshot.new_york_weather.current.location, std::string("New York"));
  EXPECT_EQ(snapshot.new_york_weather.current.condition, std::string("Sunny"));
  EXPECT_EQ(snapshot.new_york_weather.current.temperature_c, 22.0);
  EXPECT_EQ(snapshot.new_york_weather.current.rain_probability_percent, 15);
}

HOST_TEST(indoor_fixture_has_non_flat_temperature_history) {
  const app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  EXPECT_EQ(snapshot.indoor.temperature_history_c,
            (std::array<double, 8>{24.2, 24.3, 24.5, 24.6,
                                   24.7, 24.8, 24.8, 24.8}));
  EXPECT_TRUE(snapshot.indoor.temperature_history_c.front() !=
              snapshot.indoor.temperature_history_c.back());
  const auto points = ui::normalize_chart_samples(
      std::array<int, 8>{242, 243, 245, 246, 247, 248, 248, 248},
      ui::Rect{0, 0, 80, 20});
  EXPECT_TRUE(points.front().y != points.back().y);
}

HOST_TEST(forecast_fixture_contains_rain_probability_for_every_day) {
  const app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  EXPECT_EQ(snapshot.weather.seven_day[0].rain_probability_percent, 25);
  EXPECT_EQ(snapshot.weather.seven_day[1].rain_probability_percent, 70);
  EXPECT_EQ(snapshot.weather.seven_day[2].rain_probability_percent, 65);
  EXPECT_EQ(snapshot.weather.seven_day[3].rain_probability_percent, 35);
  EXPECT_EQ(snapshot.weather.seven_day[4].rain_probability_percent, 10);
  EXPECT_EQ(snapshot.weather.seven_day[5].rain_probability_percent, 30);
  EXPECT_EQ(snapshot.weather.seven_day[6].rain_probability_percent, 5);
  const auto columns = ui::forecast_columns(ui::Rect{0, 0, 400, 120});
  EXPECT_EQ(columns.back().right(), 400);
}
