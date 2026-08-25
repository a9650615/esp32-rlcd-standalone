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
  // Centred, not flush right: the cluster moved out of the tray to the bottom
  // of the page, where an off-centre indicator reads as a mistake.
  EXPECT_EQ(five.start_x, bounds.x + (bounds.width - 41) / 2);

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

HOST_TEST(weather_icon_kind_collapses_wmo_conditions_into_four_shapes) {
  using ui::WeatherIconKind;
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Clear") == WeatherIconKind::Sun);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Mostly Clear") ==
              WeatherIconKind::Sun);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Sunny") == WeatherIconKind::Sun);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Partly Cloudy") ==
              WeatherIconKind::Cloud);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Overcast") ==
              WeatherIconKind::Cloud);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Fog") == WeatherIconKind::Cloud);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Unknown") ==
              WeatherIconKind::Cloud);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("") == WeatherIconKind::Cloud);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Rain") == WeatherIconKind::Rain);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Icy Rain") ==
              WeatherIconKind::Rain);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Drizzle") ==
              WeatherIconKind::Rain);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Showers") ==
              WeatherIconKind::Rain);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Thunderstorm") ==
              WeatherIconKind::Rain);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Tstorm Hail") ==
              WeatherIconKind::Rain);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Storm") == WeatherIconKind::Rain);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Snow") == WeatherIconKind::Snow);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Snow Grains") ==
              WeatherIconKind::Snow);
  EXPECT_TRUE(ui::weather_icon_kind_for_condition("Snow Showers") ==
              WeatherIconKind::Snow);
}

HOST_TEST(home_tile_battery_overvoltage_outranks_a_weather_alert) {
  app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  snapshot.battery.valid = true;
  snapshot.battery.percent = 90;
  snapshot.battery.overvoltage_warning = true;
  snapshot.weather.valid = true;
  snapshot.weather.alert = true;
  EXPECT_TRUE(ui::choose_home_tile(snapshot) == ui::HomeTileKind::Battery);
}

HOST_TEST(home_tile_low_battery_also_outranks_a_weather_alert) {
  app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  snapshot.battery.valid = true;
  snapshot.battery.percent = ui::kHomeLowBatteryPercent;
  snapshot.battery.overvoltage_warning = false;
  snapshot.weather.valid = true;
  snapshot.weather.alert = true;
  EXPECT_TRUE(ui::choose_home_tile(snapshot) == ui::HomeTileKind::Battery);
}

HOST_TEST(home_tile_weather_alert_outranks_a_quiet_default) {
  app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  snapshot.battery.valid = true;
  snapshot.battery.percent = 90;
  snapshot.battery.overvoltage_warning = false;
  snapshot.weather.valid = true;
  snapshot.weather.alert = true;
  snapshot.taiwan_market.valid = true;
  EXPECT_TRUE(ui::choose_home_tile(snapshot) == ui::HomeTileKind::Weather);
}

HOST_TEST(home_tile_falls_back_to_weather_market_indoor_in_order_when_quiet) {
  app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  snapshot.battery.valid = false;
  snapshot.weather.valid = true;
  snapshot.weather.alert = false;
  snapshot.taiwan_market.valid = true;
  snapshot.indoor.valid = true;
  EXPECT_TRUE(ui::choose_home_tile(snapshot) == ui::HomeTileKind::Weather);

  snapshot.weather.valid = false;
  EXPECT_TRUE(ui::choose_home_tile(snapshot) == ui::HomeTileKind::Market);

  snapshot.taiwan_market.valid = false;
  EXPECT_TRUE(ui::choose_home_tile(snapshot) == ui::HomeTileKind::Indoor);

  snapshot.indoor.valid = false;
  snapshot.battery.valid = true;
  snapshot.battery.percent = 90;
  EXPECT_TRUE(ui::choose_home_tile(snapshot) == ui::HomeTileKind::Battery);
}

HOST_TEST(home_tile_skips_an_invalid_candidate_instead_of_showing_no_data) {
  app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  snapshot.battery.valid = false;
  snapshot.weather.valid = false;
  snapshot.weather.alert = true;  // alert flag on invalid data must not count
  snapshot.taiwan_market.valid = true;
  snapshot.indoor.valid = true;
  EXPECT_TRUE(ui::choose_home_tile(snapshot) == ui::HomeTileKind::Market);
}

HOST_TEST(home_tile_is_none_when_nothing_at_all_is_valid) {
  app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  snapshot.battery.valid = false;
  snapshot.weather.valid = false;
  snapshot.taiwan_market.valid = false;
  snapshot.indoor.valid = false;
  EXPECT_TRUE(ui::choose_home_tile(snapshot) == ui::HomeTileKind::None);
}

HOST_TEST(page_dots_sit_centred_along_the_bottom_below_every_page) {
  const ui::Rect canvas = ui::safe_canvas();
  const ui::Rect band = ui::page_dots_band(canvas);
  // Flush to the bottom of the canvas, spanning its full width.
  EXPECT_EQ(band.bottom(), canvas.bottom());
  EXPECT_EQ(band.x, canvas.x);

  // Centred, and centred for any page count rather than only the current one:
  // the cluster must not drift as pages are added or skipped.
  for (std::size_t count = 1; count <= 7; ++count) {
    const auto dots = ui::page_dots_geometry(band, 0, count);
    const int left = dots.start_x - band.x;
    const int right = band.right() - (dots.start_x + dots.total_width);
    // Equal margins either side, allowing one pixel for an odd remainder.
    EXPECT_TRUE(left - right <= 1 && right - left <= 1);
  }

  // The band never overlaps what the page itself draws into.
  for (const app_core::PageId page :
       {app_core::PageId::Home, app_core::PageId::TaiwanMarket,
        app_core::PageId::Weather, app_core::PageId::Indoor}) {
    EXPECT_TRUE(ui::content_bounds(canvas, page).bottom() <= band.y);
  }
}

HOST_TEST(home_shares_the_tray_and_content_area_with_the_data_pages) {
  const ui::Rect canvas = ui::safe_canvas();
  EXPECT_TRUE(ui::page_shows_tray(app_core::PageId::Home));
  // Identical content area, so the clock cannot sit at a different height from
  // the pages it alternates with.
  const ui::Rect home = ui::content_bounds(canvas, app_core::PageId::Home);
  const ui::Rect weather = ui::content_bounds(canvas, app_core::PageId::Weather);
  EXPECT_EQ(home.y, weather.y);
  EXPECT_EQ(home.height, weather.height);
  EXPECT_TRUE(home.y > canvas.y);  // the tray band is genuinely reserved

  // Pages outside the rotation carry no position marker.
  EXPECT_TRUE(!ui::page_shows_dots(app_core::PageId::Setup));
  EXPECT_TRUE(!ui::page_shows_dots(app_core::PageId::Ota));
  EXPECT_TRUE(ui::page_shows_dots(app_core::PageId::Home));
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

// Home shows two tiles when there is a second real reading and one when there
// is not - never an empty half, and never a placeholder invented to fill it.
HOST_TEST(home_shows_a_second_tile_only_when_it_has_real_data) {
  app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  snapshot.battery.valid = false;
  snapshot.weather.valid = true;
  snapshot.indoor.valid = true;
  snapshot.taiwan_market.valid = false;

  const ui::HomeTileKind first = ui::choose_home_tile(snapshot);
  const ui::HomeTileKind second = ui::choose_home_second_tile(snapshot, first);
  EXPECT_TRUE(second != ui::HomeTileKind::None);
  // The two must differ, or the same reading is shown twice.
  EXPECT_TRUE(first != second);

  // With only one valid source there is no second tile.
  snapshot.indoor.valid = false;
  snapshot.taiwan_market.valid = false;
  snapshot.battery.valid = false;
  const ui::HomeTileKind only = ui::choose_home_tile(snapshot);
  EXPECT_TRUE(ui::choose_home_second_tile(snapshot, only) ==
              ui::HomeTileKind::None);
}

HOST_TEST(home_tile_cells_split_the_column_without_overlapping) {
  const ui::Rect column{270, 42, 118, 244};

  // One tile keeps the whole column: a sparse board shows no empty half.
  const ui::Rect single = ui::home_tile_cell(column, 0, 1);
  EXPECT_EQ(single.height, column.height);
  EXPECT_EQ(single.y, column.y);

  const ui::Rect top = ui::home_tile_cell(column, 0, 2);
  const ui::Rect bottom = ui::home_tile_cell(column, 1, 2);
  EXPECT_TRUE(top.bottom() < bottom.y);              // gap between them
  EXPECT_TRUE(bottom.bottom() <= column.bottom());   // stays in the column
  EXPECT_EQ(top.height, bottom.height);              // evenly split
  // Each half still has room for tile_text_layout's actual stacked content
  // (title+value+detail = kTileContentHeight, ui_theme.hpp) - not a
  // separately-guessed number. A hardcoded "big enough" figure here would
  // prove nothing about the content this cell is actually for, which is
  // exactly the gap that let the settings page's status line overflow
  // unnoticed (see ui_data.hpp's settings_layout_fits / kSettingsRowHeight).
  EXPECT_TRUE(top.height >= ui::kTileContentHeight);
}

// settings_layout_fits existed for a while covering every rect the layout
// produces - it just was never wired into anything, compile-time or
// runtime, so the status line overflowing content_bounds by 6px sat there
// unasserted rather than unfixable. This is the runtime half of the fix
// (ui_data.hpp's static_assert is the compile-time half) - both check the
// exact same invariant on purpose, the same belt-and-suspenders pattern
// ota_layout_fits already uses (see test_ota.cpp).
HOST_TEST(settings_layout_fits_and_rows_do_not_overlap) {
  const ui::Rect content =
      ui::content_bounds(ui::safe_canvas(), app_core::PageId::Settings);
  EXPECT_TRUE(ui::settings_layout_fits(content));

  const ui::SettingsLayout layout = ui::settings_layout(content);
  EXPECT_TRUE(!ui::rects_intersect(layout.title, layout.rows[0]));
  for (int i = 0; i + 1 < ui::kSettingsRowCount; ++i) {
    EXPECT_TRUE(!ui::rects_intersect(layout.rows[i], layout.rows[i + 1]));
  }
  EXPECT_TRUE(!ui::rects_intersect(layout.rows[ui::kSettingsRowCount - 1],
                                   layout.status));

  // The regression this whole fix exists to catch, spelled out rather than
  // left implicit in settings_layout_fits: the status line - the element
  // that actually overflowed - must end at or before the tray-reduced
  // content bounds, not just somewhere inside the wider safe canvas.
  EXPECT_TRUE(layout.status.bottom() <= content.bottom());
}

HOST_TEST(battery_percent_is_trustworthy_only_when_valid_and_not_charging) {
  app_core::BatteryData battery;
  battery.valid = true;
  battery.charging = false;
  EXPECT_TRUE(ui::battery_percent_trustworthy(battery,
                                              app_core::PowerTrend::Steady));
  EXPECT_TRUE(ui::battery_percent_trustworthy(
      battery, app_core::PowerTrend::Discharging));

  // Invalid reading: no percentage was ever computed to trust.
  battery.valid = false;
  EXPECT_TRUE(!ui::battery_percent_trustworthy(battery,
                                               app_core::PowerTrend::Steady));

  // The fast, voltage-based signal (BatteryData::charging) withholds trust
  // on its own, regardless of what the slower PowerTrend says.
  battery.valid = true;
  battery.charging = true;
  EXPECT_TRUE(!ui::battery_percent_trustworthy(battery,
                                               app_core::PowerTrend::Steady));

  // The slow, slope-based signal (PowerTrend::Charging) also withholds
  // trust on its own, even when the fast signal has not (yet) caught up -
  // this is exactly the early-charge case (a depleted cell charging well
  // below the ~4.15 V fast threshold) the fast signal alone would miss.
  battery.charging = false;
  EXPECT_TRUE(!ui::battery_percent_trustworthy(
      battery, app_core::PowerTrend::Charging));
}

// battery_percent_trustworthy() above is defined as
// "valid && !battery_is_charging(...)" - this is the direct test of the
// half that changed meaning: battery_is_charging() itself, which the tray
// icon now also calls on its own (it needs to know "charging" independent
// of "valid", unlike the settings row and Home tile, which only ever need
// the combined trustworthy answer).
HOST_TEST(a_measured_direction_outranks_a_two_hour_old_trend) {
  // The regression test for a bolt that stayed on the tray for up to two
  // hours after the cable came out. The voltage signal had already said no
  // within one 30 s publish; the trend was still fitting the charge that had
  // just ended, and an OR let it win.
  app_core::BatteryData battery;
  battery.valid = true;
  battery.charging = false;
  battery.direction_known = true;
  EXPECT_TRUE(
      !ui::battery_is_charging(battery, app_core::PowerTrend::Charging));
  // ...and the percentage is trustworthy again the moment it is not
  // charging, rather than being withheld for the same two hours.
  EXPECT_TRUE(ui::battery_percent_trustworthy(
      battery, app_core::PowerTrend::Charging));

  // The trend still answers while nothing else can: the first eleven minutes
  // of a boot, before the slope window has spanned enough to be fitted.
  // "No data" must not read as "not charging" - a board that booted on a
  // charger would show a discharging icon.
  battery.direction_known = false;
  EXPECT_TRUE(ui::battery_is_charging(battery, app_core::PowerTrend::Charging));

  // And a measured yes is still a yes regardless of the trend.
  battery.direction_known = true;
  battery.charging = true;
  EXPECT_TRUE(
      ui::battery_is_charging(battery, app_core::PowerTrend::Discharging));
}

HOST_TEST(the_trend_is_the_fallback_while_no_direction_has_been_measured) {
  // Every case here leaves direction_known false, which is the only state in
  // which the trend still has a say - see the test above for what happens
  // once the voltage signal has measured something. Named for that rather
  // than for the OR it used to be, because the OR is now half the rule.
  app_core::BatteryData battery;
  battery.charging = false;
  EXPECT_TRUE(!ui::battery_is_charging(battery, app_core::PowerTrend::Steady));
  EXPECT_TRUE(
      !ui::battery_is_charging(battery, app_core::PowerTrend::Discharging));

  battery.charging = true;
  EXPECT_TRUE(ui::battery_is_charging(battery, app_core::PowerTrend::Steady));

  battery.charging = false;
  EXPECT_TRUE(ui::battery_is_charging(battery, app_core::PowerTrend::Charging));

  // Deliberately independent of BatteryData::valid: an invalid reading is a
  // separate concern (nothing was measured) from charging (something was
  // measured, and it looks like a charger). battery_percent_trustworthy()
  // is where the two get combined into one "should this row show a
  // number" answer; this function alone must not require valid.
  battery.valid = false;
  battery.charging = true;
  EXPECT_TRUE(ui::battery_is_charging(battery, app_core::PowerTrend::Steady));
}
