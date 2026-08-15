#include "app_snapshot.hpp"

namespace app_core {
namespace {

bool decode_bcd(uint8_t value, uint8_t mask, uint8_t maximum,
                uint8_t& decoded) {
  value &= mask;
  const uint8_t low = value & 0x0f;
  const uint8_t high = static_cast<uint8_t>((value >> 4) & 0x0f);
  if (low > 9 || high > 9) return false;
  decoded = static_cast<uint8_t>(high * 10 + low);
  return decoded <= maximum;
}

uint8_t days_in_month(uint16_t year, uint8_t month) {
  static constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};
  if (month == 2 &&
      (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
    return 29;
  }
  return days[month - 1];
}

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

bool decode_pcf85063(const uint8_t* registers, std::size_t length,
                     RtcDateTime& decoded) {
  if (registers == nullptr || length < 7) return false;

  uint8_t second = 0;
  uint8_t minute = 0;
  uint8_t hour = 0;
  uint8_t day = 0;
  uint8_t weekday = 0;
  uint8_t month = 0;
  uint8_t year = 0;
  if (!decode_bcd(registers[0], 0x7f, 59, second) ||
      !decode_bcd(registers[1], 0x7f, 59, minute) ||
      !decode_bcd(registers[2], 0x3f, 23, hour) ||
      !decode_bcd(registers[3], 0x3f, 31, day) || day == 0 ||
      !decode_bcd(registers[4], 0x07, 6, weekday) ||
      !decode_bcd(registers[5], 0x1f, 12, month) || month == 0 ||
      !decode_bcd(registers[6], 0xff, 99, year)) {
    return false;
  }
  (void)weekday;
  const uint16_t full_year = static_cast<uint16_t>(2000 + year);
  if (day > days_in_month(full_year, month)) return false;
  decoded = {full_year, month, day,
             hour, minute, second};
  return true;
}

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
