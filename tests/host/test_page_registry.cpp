#include "test_support.hpp"

#include "app_snapshot.hpp"
#include "page_registry.hpp"

#include <array>
#include <string>

using app_core::AppSnapshot;
using app_core::DemoScenario;
using app_core::PageId;
using app_core::PageRegistry;
using app_core::make_mock_snapshot;

HOST_TEST(registry_has_all_five_pages_when_data_is_available) {
  const AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  PageRegistry registry;
  registry.begin_cycle(snapshot);
#ifdef APP_CORE_DEMO_MISSING_PAGE
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::TaiwanMarket,
                                 PageId::UsMarket, PageId::Indoor}));
#else
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::TaiwanMarket,
                                 PageId::UsMarket, PageId::Weather,
                                 PageId::Indoor}));
#endif
}

HOST_TEST(registry_omits_unavailable_pages) {
  AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  snapshot.availability.weather = false;
  PageRegistry registry;
  registry.begin_cycle(snapshot);
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::TaiwanMarket,
                                 PageId::UsMarket, PageId::Indoor}));
}

HOST_TEST(morning_alert_orders_weather_before_other_data_pages) {
  const AppSnapshot snapshot = make_mock_snapshot(DemoScenario::MorningAlert);
  PageRegistry registry;
  registry.begin_cycle(snapshot);
#ifdef APP_CORE_DEMO_MISSING_PAGE
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::TaiwanMarket,
                                 PageId::UsMarket, PageId::Indoor}));
#else
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::Weather,
                                 PageId::TaiwanMarket, PageId::UsMarket,
                                 PageId::Indoor}));
#endif
}

HOST_TEST(taiwan_session_orders_taiwan_market_first) {
  const AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  PageRegistry registry;
  registry.begin_cycle(snapshot);
#ifdef APP_CORE_DEMO_MISSING_PAGE
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::TaiwanMarket,
                                 PageId::UsMarket, PageId::Indoor}));
#else
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::TaiwanMarket,
                                 PageId::UsMarket, PageId::Weather,
                                 PageId::Indoor}));
#endif
}

HOST_TEST(night_session_orders_us_market_first) {
  const AppSnapshot snapshot = make_mock_snapshot(DemoScenario::NightSession);
  PageRegistry registry;
  registry.begin_cycle(snapshot);
#ifdef APP_CORE_DEMO_MISSING_PAGE
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::UsMarket,
                                 PageId::TaiwanMarket, PageId::Indoor}));
#else
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::UsMarket,
                                 PageId::Weather, PageId::TaiwanMarket,
                                 PageId::Indoor}));
#endif
}

HOST_TEST(registry_does_not_reorder_in_the_middle_of_a_cycle) {
  AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  PageRegistry registry;
  registry.begin_cycle(snapshot);
  const auto started_order = registry.page_ids();

  snapshot.scenario = DemoScenario::NightSession;
  EXPECT_EQ(registry.page_ids(), started_order);

  registry.begin_cycle(snapshot);
  EXPECT_EQ(registry.page_ids()[1], PageId::UsMarket);
}

HOST_TEST(mock_fixture_contains_required_deterministic_content) {
  const AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);

  EXPECT_EQ(snapshot.clock.hero, std::string("09:41"));
  EXPECT_EQ(snapshot.clock.date, std::string("Sat, 15 Aug 2026"));
  EXPECT_EQ(snapshot.clock.source, std::string("Clock Hero"));

  EXPECT_EQ(snapshot.taiwan_market.primary_label, std::string("TAIEX"));
  EXPECT_EQ(snapshot.taiwan_market.primary_value, 24'334);
  EXPECT_EQ(snapshot.taiwan_market.primary_change_percent, 0.52);
  EXPECT_EQ(snapshot.taiwan_market.secondary_label, std::string("TW50"));
  EXPECT_EQ(snapshot.taiwan_market.secondary_change_percent, 0.44);
  EXPECT_EQ(snapshot.taiwan_market.secondary_value, 20'871);

  EXPECT_EQ(snapshot.us_market.display_name, std::string("US Market"));
  EXPECT_EQ(snapshot.us_market.primary_label, std::string("S&P 500"));
  EXPECT_EQ(snapshot.us_market.primary_value, 5'432);
  EXPECT_EQ(snapshot.us_market.primary_change_percent, -0.18);
  EXPECT_EQ(snapshot.us_market.secondary_label, std::string("NASDAQ"));
  EXPECT_EQ(snapshot.us_market.secondary_value, 17'667);
  EXPECT_EQ(snapshot.us_market.secondary_change_percent, 0.21);

  EXPECT_EQ(snapshot.weather.current.location, std::string("Taipei"));
  EXPECT_EQ(snapshot.weather.current.condition, std::string("Cloudy"));
  EXPECT_EQ(snapshot.weather.current.temperature_c, 29.0);
  EXPECT_EQ(snapshot.weather.current.rain_probability_percent, 40);
  EXPECT_EQ(snapshot.new_york_weather.current.location, std::string("New York"));
  EXPECT_EQ(snapshot.new_york_weather.current.condition, std::string("Sunny"));
  EXPECT_EQ(snapshot.new_york_weather.current.temperature_c, 22.0);
  EXPECT_EQ(snapshot.new_york_weather.current.rain_probability_percent, 15);
  EXPECT_EQ(snapshot.weather.seven_day.size(), static_cast<size_t>(7));
  const std::array<app_core::WeatherDay, 7> expected_forecast = {{
      {"Sat", "Cloudy", 30.0, 25.0, 25},
      {"Sun", "Rain", 28.0, 24.0, 70},
      {"Mon", "Rain", 27.0, 23.0, 65},
      {"Tue", "Cloudy", 29.0, 24.0, 35},
      {"Wed", "Sunny", 31.0, 25.0, 10},
      {"Thu", "Cloudy", 30.0, 25.0, 30},
      {"Fri", "Sunny", 32.0, 26.0, 5},
  }};
  for (size_t i = 0; i < expected_forecast.size(); ++i) {
    EXPECT_EQ(snapshot.weather.seven_day[i].day, expected_forecast[i].day);
    EXPECT_EQ(snapshot.weather.seven_day[i].condition,
              expected_forecast[i].condition);
    EXPECT_EQ(snapshot.weather.seven_day[i].high_c, expected_forecast[i].high_c);
    EXPECT_EQ(snapshot.weather.seven_day[i].low_c, expected_forecast[i].low_c);
    EXPECT_EQ(snapshot.weather.seven_day[i].rain_probability_percent,
              expected_forecast[i].rain_probability_percent);
  }

  EXPECT_EQ(snapshot.indoor.temperature_c, 24.8);
  EXPECT_EQ(snapshot.indoor.humidity_percent, 57);
  EXPECT_EQ(snapshot.indoor.temperature_history_c,
            (std::array<double, 8>{24.2, 24.3, 24.5, 24.6,
                                   24.7, 24.8, 24.8, 24.8}));

  EXPECT_EQ(snapshot.taiwan_market.intraday_samples,
            (std::array<int, 8>{24'060, 24'110, 24'095, 24'180,
                                24'240, 24'220, 24'300, 24'334}));
  EXPECT_EQ(snapshot.us_market.intraday_samples,
            (std::array<int, 8>{5'410, 5'425, 5'420, 5'438,
                                5'430, 5'440, 5'426, 5'432}));
}

HOST_TEST(pcf85063_decode_rejects_invalid_bcd_and_ranges) {
  app_core::RtcDateTime decoded{};
  const std::array<uint8_t, 7> valid = {0x30, 0x41, 0x09, 0x15, 0x06, 0x08,
                                        0x26};
  EXPECT_TRUE(app_core::decode_pcf85063(valid.data(), valid.size(), decoded));
  EXPECT_EQ(decoded.year, static_cast<uint16_t>(2026));
  EXPECT_EQ(decoded.month, static_cast<uint8_t>(8));
  EXPECT_EQ(decoded.day, static_cast<uint8_t>(15));
  EXPECT_EQ(decoded.hour, static_cast<uint8_t>(9));
  EXPECT_EQ(decoded.minute, static_cast<uint8_t>(41));
  EXPECT_EQ(decoded.second, static_cast<uint8_t>(30));

  const std::array<uint8_t, 7> invalid_bcd = {0x70, 0x41, 0x09, 0x15,
                                              0x06, 0x08, 0x26};
  EXPECT_TRUE(!app_core::decode_pcf85063(invalid_bcd.data(), invalid_bcd.size(),
                                         decoded));
  const std::array<uint8_t, 7> invalid_range = {0x30, 0x61, 0x09, 0x15,
                                                0x06, 0x08, 0x26};
  EXPECT_TRUE(!app_core::decode_pcf85063(invalid_range.data(), invalid_range.size(),
                                         decoded));
  const std::array<uint8_t, 7> invalid_calendar = {0x30, 0x41, 0x09, 0x31,
                                                    0x02, 0x02, 0x23};
  EXPECT_TRUE(!app_core::decode_pcf85063(invalid_calendar.data(),
                                         invalid_calendar.size(), decoded));
}

HOST_TEST(auto_rotation_skips_a_weekday_market_page_only_when_data_invalid) {
  AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  snapshot.clock.source = "SNTP";
  snapshot.clock.date = "Wed, 12 Aug 2026";
  snapshot.taiwan_market.valid = true;
  EXPECT_TRUE(app_core::page_relevant_for_auto_rotation(PageId::TaiwanMarket,
                                                        snapshot));
  snapshot.taiwan_market.valid = false;
  EXPECT_TRUE(!app_core::page_relevant_for_auto_rotation(PageId::TaiwanMarket,
                                                         snapshot));
}

HOST_TEST(auto_rotation_skips_market_pages_on_a_taipei_weekend) {
  AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  snapshot.taiwan_market.valid = true;
  snapshot.us_market.valid = true;
  snapshot.clock.source = "SNTP";

  snapshot.clock.date = "Sat, 15 Aug 2026";
  EXPECT_TRUE(!app_core::page_relevant_for_auto_rotation(PageId::TaiwanMarket,
                                                         snapshot));
  EXPECT_TRUE(!app_core::page_relevant_for_auto_rotation(PageId::UsMarket,
                                                         snapshot));

  snapshot.clock.date = "Sun, 16 Aug 2026";
  EXPECT_TRUE(!app_core::page_relevant_for_auto_rotation(PageId::TaiwanMarket,
                                                         snapshot));

  // A non-market page is unaffected by the weekend signal.
  snapshot.weather.valid = true;
  EXPECT_TRUE(
      app_core::page_relevant_for_auto_rotation(PageId::Weather, snapshot));
}

HOST_TEST(auto_rotation_weekend_signal_requires_a_real_synced_clock) {
  AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  snapshot.taiwan_market.valid = true;
  snapshot.clock.date = "Sat, 15 Aug 2026";
  snapshot.clock.source = "RTC fallback";
  EXPECT_TRUE(
      app_core::page_relevant_for_auto_rotation(PageId::TaiwanMarket, snapshot));
}

HOST_TEST(auto_rotation_invalid_data_is_skipped_on_any_page_kind) {
  AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  snapshot.weather.valid = false;
  snapshot.indoor.valid = false;
  EXPECT_TRUE(
      !app_core::page_relevant_for_auto_rotation(PageId::Weather, snapshot));
  EXPECT_TRUE(
      !app_core::page_relevant_for_auto_rotation(PageId::Indoor, snapshot));
  EXPECT_TRUE(
      app_core::page_relevant_for_auto_rotation(PageId::Home, snapshot));
}

HOST_TEST(next_relevant_auto_index_skips_forward_past_irrelevant_pages) {
  AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  snapshot.clock.source = "SNTP";
  snapshot.clock.date = "Sat, 15 Aug 2026";
  snapshot.taiwan_market.valid = true;
  snapshot.us_market.valid = true;
  snapshot.weather.valid = false;
  snapshot.indoor.valid = true;
  const std::vector<PageId> pages{PageId::Home, PageId::TaiwanMarket,
                                  PageId::UsMarket, PageId::Weather,
                                  PageId::Indoor};
  // Landing on TaiwanMarket (closed weekend) should skip to Indoor, past the
  // also-closed UsMarket and the invalid Weather page.
  EXPECT_TRUE(app_core::next_relevant_auto_index(pages, 1, snapshot) ==
              static_cast<std::size_t>(4));
  // Landing on an already-relevant page is a no-op.
  EXPECT_TRUE(app_core::next_relevant_auto_index(pages, 4, snapshot) ==
              static_cast<std::size_t>(4));
}

HOST_TEST(next_relevant_auto_index_never_ends_up_with_nothing_to_show) {
  AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  snapshot.clock.source = "SNTP";
  snapshot.clock.date = "Sat, 15 Aug 2026";
  snapshot.taiwan_market.valid = true;
  snapshot.us_market.valid = true;
  // Every page in this rotation is either a closed-weekend market or
  // invalid data - nothing qualifies, so the fallback must return the
  // landed-on index unchanged rather than searching forever.
  const std::vector<PageId> pages{PageId::TaiwanMarket, PageId::UsMarket};
  EXPECT_TRUE(app_core::next_relevant_auto_index(pages, 0, snapshot) ==
              static_cast<std::size_t>(0));
  EXPECT_TRUE(app_core::next_relevant_auto_index(pages, 1, snapshot) ==
              static_cast<std::size_t>(1));
}

HOST_TEST(next_relevant_auto_index_empty_pages_is_a_safe_noop) {
  AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  const std::vector<PageId> pages{};
  EXPECT_TRUE(app_core::next_relevant_auto_index(pages, 0, snapshot) ==
              static_cast<std::size_t>(0));
}

HOST_TEST(fallback_clock_advances_across_midnight_and_leap_day) {
  const app_core::RtcDateTime start{2024, 2, 28, 23, 59, 50};
  const app_core::RtcDateTime next =
      app_core::advance_rtc_datetime(start, 24 * 60 * 60 + 24 * 60 + 15);
  EXPECT_EQ(next.year, static_cast<uint16_t>(2024));
  EXPECT_EQ(next.month, static_cast<uint8_t>(3));
  EXPECT_EQ(next.day, static_cast<uint8_t>(1));
  EXPECT_EQ(next.hour, static_cast<uint8_t>(0));
  EXPECT_EQ(next.minute, static_cast<uint8_t>(24));
  EXPECT_EQ(next.second, static_cast<uint8_t>(5));
}
