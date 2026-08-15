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
  weather.seven_day = {{{"Sat", "Cloudy", 30.0, 25.0, 25},
                        {"Sun", "Rain", 28.0, 24.0, 70},
                        {"Mon", "Rain", 27.0, 23.0, 65},
                        {"Tue", "Cloudy", 29.0, 24.0, 35},
                        {"Wed", "Sunny", 31.0, 25.0, 10},
                        {"Thu", "Cloudy", 30.0, 25.0, 30},
                        {"Fri", "Sunny", 32.0, 26.0, 5}}};
  return weather;
}

WeatherData new_york_weather() {
  WeatherData weather;
  weather.current = {"New York", "Sunny", 22.0, 15};
  weather.alert = false;
  weather.seven_day = {{{"Sat", "Sunny", 24.0, 18.0, 10},
                        {"Sun", "Cloudy", 23.0, 17.0, 25},
                        {"Mon", "Rain", 21.0, 16.0, 60},
                        {"Tue", "Cloudy", 22.0, 17.0, 35},
                        {"Wed", "Sunny", 25.0, 18.0, 10},
                        {"Thu", "Sunny", 26.0, 19.0, 5},
                        {"Fri", "Cloudy", 24.0, 18.0, 20}}};
  return weather;
}

}  // namespace

AppSnapshot make_mock_snapshot(DemoScenario scenario) {
  AppSnapshot snapshot;
  snapshot.clock = {"09:41", "Sat, 15 Aug 2026", "Clock Hero"};
  snapshot.taiwan_market = taiwan_market();
  snapshot.us_market = us_market();
  snapshot.weather = taipei_weather();
  snapshot.new_york_weather = new_york_weather();
  snapshot.indoor = {24.8, 57, {24.2, 24.3, 24.5, 24.6,
                                 24.7, 24.8, 24.8, 24.8}};
  snapshot.availability = {};
#ifdef APP_CORE_DEMO_MISSING_PAGE
  snapshot.availability.weather = false;
#endif
  snapshot.scenario = scenario;
  return snapshot;
}

}  // namespace app_core
