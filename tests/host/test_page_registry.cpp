#include "test_support.hpp"

#include "app_snapshot.hpp"
#include "page_registry.hpp"

#include <array>

using app_core::AppSnapshot;
using app_core::DemoScenario;
using app_core::PageId;
using app_core::PageRegistry;
using app_core::make_mock_snapshot;

HOST_TEST(registry_has_all_five_pages_when_data_is_available) {
  const AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  PageRegistry registry;
  registry.begin_cycle(snapshot);
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::TaiwanMarket,
                                 PageId::UsMarket, PageId::Weather,
                                 PageId::Indoor}));
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
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::Weather,
                                 PageId::TaiwanMarket, PageId::UsMarket,
                                 PageId::Indoor}));
}

HOST_TEST(taiwan_session_orders_taiwan_market_first) {
  const AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  PageRegistry registry;
  registry.begin_cycle(snapshot);
  EXPECT_EQ(registry.page_ids()[1], PageId::TaiwanMarket);
}

HOST_TEST(night_session_orders_us_market_first) {
  const AppSnapshot snapshot = make_mock_snapshot(DemoScenario::NightSession);
  PageRegistry registry;
  registry.begin_cycle(snapshot);
  EXPECT_EQ(registry.page_ids(),
            (std::vector<PageId>{PageId::Home, PageId::UsMarket,
                                 PageId::Weather, PageId::TaiwanMarket,
                                 PageId::Indoor}));
}

HOST_TEST(registry_does_not_reorder_in_the_middle_of_a_cycle) {
  AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  PageRegistry registry;
  registry.begin_cycle(snapshot);
  const auto started_order = registry.page_ids();

  snapshot.scenario = DemoScenario::NightSession;
  registry.observe(snapshot);
  EXPECT_EQ(registry.page_ids(), started_order);

  registry.begin_cycle(snapshot);
  EXPECT_EQ(registry.page_ids()[1], PageId::UsMarket);
}
