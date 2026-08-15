#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace app_core {

enum class PageId { Home, TaiwanMarket, UsMarket, Weather, Indoor };
enum class DemoScenario { MorningAlert, TaiwanSession, NightSession };

struct ClockData {
  std::string hero;
  std::string date;
  std::string source;
};

struct MarketData {
  std::string display_name;
  std::string primary_label;
  int primary_value = 0;
  double primary_change_percent = 0.0;
  std::string secondary_label;
  int secondary_value = 0;
  double secondary_change_percent = 0.0;
  std::array<int, 8> intraday_samples{};
};

struct WeatherCurrent {
  std::string location;
  std::string condition;
  double temperature_c = 0.0;
  uint8_t rain_probability_percent = 0;
};

struct WeatherDay {
  std::string day;
  std::string condition;
  double high_c = 0.0;
  double low_c = 0.0;
  uint8_t rain_probability_percent = 0;
};

struct WeatherData {
  WeatherCurrent current;
  std::array<WeatherDay, 7> seven_day{};
  bool alert = false;
};

struct IndoorData {
  double temperature_c = 0.0;
  uint8_t humidity_percent = 0;
  // Deterministic mock history for this snapshot-only slice.
  std::array<double, 8> temperature_history_c{};
};

struct Availability {
  bool taiwan_market = true;
  bool us_market = true;
  bool weather = true;
  bool indoor = true;
};

struct AppSnapshot {
  ClockData clock;
  MarketData taiwan_market;
  MarketData us_market;
  WeatherData weather;
  WeatherData new_york_weather;
  IndoorData indoor;
  Availability availability;
  DemoScenario scenario = DemoScenario::TaiwanSession;
};

AppSnapshot make_mock_snapshot(DemoScenario scenario);

}  // namespace app_core
