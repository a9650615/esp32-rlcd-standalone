#include "app_snapshot.hpp"

namespace app_core {
namespace {

MarketData taiwan_market() {
  MarketData market;
  market.display_name = "Taiwan Market";
  market.primary_label = "TAIEX";
  market.primary_value = 24'334;
  market.primary_change_percent = 0.52;
  market.secondary_label = "TW50";
  market.secondary_value = 20'871;
  market.secondary_change_percent = 0.44;
  market.intraday_samples = {24'060, 24'110, 24'095, 24'180,
                             24'240, 24'220, 24'300, 24'334};
  return market;
}

MarketData us_market() {
  MarketData market;
  market.display_name = "US Market";
  market.primary_label = "S&P 500";
  market.primary_value = 5'432;
  market.primary_change_percent = -0.18;
  market.secondary_label = "NASDAQ";
  market.secondary_value = 17'667;
  market.secondary_change_percent = 0.21;
  market.intraday_samples = {5'410, 5'425, 5'420, 5'438,
                             5'430, 5'440, 5'426, 5'432};
  return market;
}

WeatherData taipei_weather() {
  WeatherData weather;
  weather.current = {"Taipei", "Cloudy", 29.0, 40};
  weather.alert = true;
  weather.seven_day = {{{"Sat", "Cloudy", 30.0, 25.0},
                        {"Sun", "Rain", 28.0, 24.0},
                        {"Mon", "Rain", 27.0, 23.0},
                        {"Tue", "Cloudy", 29.0, 24.0},
                        {"Wed", "Sunny", 31.0, 25.0},
                        {"Thu", "Cloudy", 30.0, 25.0},
                        {"Fri", "Sunny", 32.0, 26.0}}};
  return weather;
}

}  // namespace

AppSnapshot make_mock_snapshot(DemoScenario scenario) {
  AppSnapshot snapshot;
  snapshot.clock = {"09:41", "Sat, 15 Aug 2026", "Clock Hero"};
  snapshot.taiwan_market = taiwan_market();
  snapshot.us_market = us_market();
  snapshot.weather = taipei_weather();
  snapshot.indoor = {24.8, 57};
  snapshot.availability = {};
#ifdef APP_CORE_DEMO_MISSING_PAGE
  snapshot.availability.weather = false;
#endif
  snapshot.scenario = scenario;
  return snapshot;
}

}  // namespace app_core
