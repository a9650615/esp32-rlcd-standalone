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
