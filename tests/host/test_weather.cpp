#include "weather_parse.hpp"

#include "app_snapshot.hpp"
#include "test_support.hpp"

#include <cstring>

namespace {

// A realistic Open-Meteo /v1/forecast response: current conditions plus a
// full 7-day daily block. Dates are consecutive so the weekday-derivation
// (Sakamoto's algorithm in weather_parse.cpp) can be checked against known
// weekdays: 2026-08-16 is a Sunday.
constexpr char kFullResponse[] = R"JSON({
  "latitude": 25.03,
  "longitude": 121.56,
  "current": {
    "time": "2026-08-16T09:00",
    "interval": 900,
    "temperature_2m": 29.4,
    "weather_code": 3
  },
  "daily": {
    "time": ["2026-08-16", "2026-08-17", "2026-08-18", "2026-08-19",
              "2026-08-20", "2026-08-21", "2026-08-22"],
    "weather_code": [3, 61, 95, 0, 2, 1, 45],
    "temperature_2m_max": [31.2, 28.5, 27.0, 33.1, 30.4, 29.9, 26.6],
    "temperature_2m_min": [25.1, 24.0, 23.5, 25.8, 24.9, 24.2, 22.8],
    "precipitation_probability_max": [40, 80, 90, 5, 20, 15, 60]
  }
})JSON";

}  // namespace

HOST_TEST(weather_parse_forecast_full_response_is_valid_with_seven_days) {
  app_core::WeatherData data;
  const bool ok = weather::parse_forecast_json(
      kFullResponse, std::strlen(kFullResponse), data);

  EXPECT_TRUE(ok);
  EXPECT_TRUE(data.valid);
  EXPECT_TRUE(!data.stale);

  EXPECT_TRUE(data.current.temperature_c > 29.3 &&
              data.current.temperature_c < 29.5);
  EXPECT_EQ(data.current.condition, std::string("Overcast"));
  // Today's (index 0) daily max is used as the "current" rain probability
  // proxy, since Open-Meteo has no current-block field for it.
  EXPECT_EQ(static_cast<int>(data.current.rain_probability_percent), 40);

  EXPECT_EQ(data.seven_day[0].day, std::string("Sun"));
  EXPECT_EQ(data.seven_day[1].day, std::string("Mon"));
  EXPECT_EQ(data.seven_day[2].day, std::string("Tue"));
  EXPECT_EQ(data.seven_day[3].day, std::string("Wed"));
  EXPECT_EQ(data.seven_day[4].day, std::string("Thu"));
  EXPECT_EQ(data.seven_day[5].day, std::string("Fri"));
  EXPECT_EQ(data.seven_day[6].day, std::string("Sat"));

  EXPECT_EQ(data.seven_day[1].condition, std::string("Rain"));
  EXPECT_EQ(data.seven_day[2].condition, std::string("Thunderstorm"));
  EXPECT_TRUE(data.seven_day[3].high_c > 33.0 && data.seven_day[3].high_c < 33.2);
  EXPECT_TRUE(data.seven_day[3].low_c > 25.7 && data.seven_day[3].low_c < 25.9);
  EXPECT_EQ(static_cast<int>(data.seven_day[2].rain_probability_percent), 90);
}

HOST_TEST(weather_parse_forecast_truncated_body_is_invalid) {
  // Cut the body off mid-array; must not silently parse a partial forecast.
  const std::size_t cut = std::strlen(kFullResponse) / 2;

  app_core::WeatherData data;
  data.valid = true;  // pre-seed with a "good" value to prove it gets reset.
  const bool ok = weather::parse_forecast_json(kFullResponse, cut, data);

  EXPECT_TRUE(!ok);
  EXPECT_TRUE(!data.valid);
}

HOST_TEST(weather_parse_forecast_missing_required_field_is_invalid) {
  // Same shape as kFullResponse but daily.weather_code is missing entirely -
  // a partial JSON object, not truncated bytes.
  constexpr char kMissingField[] = R"JSON({
    "current": {
      "temperature_2m": 29.4,
      "weather_code": 3
    },
    "daily": {
      "time": ["2026-08-16", "2026-08-17", "2026-08-18", "2026-08-19",
                "2026-08-20", "2026-08-21", "2026-08-22"],
      "temperature_2m_max": [31.2, 28.5, 27.0, 33.1, 30.4, 29.9, 26.6],
      "temperature_2m_min": [25.1, 24.0, 23.5, 25.8, 24.9, 24.2, 22.8],
      "precipitation_probability_max": [40, 80, 90, 5, 20, 15, 60]
    }
  })JSON";

  app_core::WeatherData data;
  const bool ok = weather::parse_forecast_json(
      kMissingField, std::strlen(kMissingField), data);

  EXPECT_TRUE(!ok);
  EXPECT_TRUE(!data.valid);
}

HOST_TEST(weather_parse_forecast_short_daily_arrays_is_invalid) {
  // Only 3 days supplied where 7 are required - a "partial" forecast that
  // is syntactically well-formed JSON but must still be rejected.
  constexpr char kShortForecast[] = R"JSON({
    "current": {"temperature_2m": 20.0, "weather_code": 0},
    "daily": {
      "time": ["2026-08-16", "2026-08-17", "2026-08-18"],
      "weather_code": [0, 1, 2],
      "temperature_2m_max": [22.0, 23.0, 24.0],
      "temperature_2m_min": [15.0, 16.0, 17.0],
      "precipitation_probability_max": [0, 5, 10]
    }
  })JSON";

  app_core::WeatherData data;
  const bool ok = weather::parse_forecast_json(
      kShortForecast, std::strlen(kShortForecast), data);

  EXPECT_TRUE(!ok);
  EXPECT_TRUE(!data.valid);
}

HOST_TEST(weather_condition_for_wmo_code_maps_known_codes) {
  EXPECT_EQ(std::string(weather::condition_for_wmo_code(0)), std::string("Clear"));
  EXPECT_EQ(std::string(weather::condition_for_wmo_code(2)),
            std::string("Partly Cloudy"));
  EXPECT_EQ(std::string(weather::condition_for_wmo_code(45)), std::string("Fog"));
  EXPECT_EQ(std::string(weather::condition_for_wmo_code(63)), std::string("Rain"));
  EXPECT_EQ(std::string(weather::condition_for_wmo_code(75)), std::string("Snow"));
  EXPECT_EQ(std::string(weather::condition_for_wmo_code(95)),
            std::string("Thunderstorm"));
  EXPECT_EQ(std::string(weather::condition_for_wmo_code(99)),
            std::string("Tstorm Hail"));
}

HOST_TEST(weather_condition_for_wmo_code_maps_unknown_code_safely) {
  EXPECT_EQ(std::string(weather::condition_for_wmo_code(12345)),
            std::string("Unknown"));
  EXPECT_EQ(std::string(weather::condition_for_wmo_code(-1)),
            std::string("Unknown"));
}

HOST_TEST(weather_parse_geolocation_extracts_lat_lon_on_success) {
  constexpr char kGeoResponse[] = R"JSON({
    "ip": "203.0.113.42",
    "success": true,
    "type": "IPv4",
    "latitude": 25.0375,
    "longitude": 121.5637
  })JSON";

  double latitude = 0.0;
  double longitude = 0.0;
  const bool ok = weather::parse_geolocation_json(
      kGeoResponse, std::strlen(kGeoResponse), latitude, longitude);

  EXPECT_TRUE(ok);
  EXPECT_TRUE(latitude > 25.03 && latitude < 25.04);
  EXPECT_TRUE(longitude > 121.56 && longitude < 121.57);
}

HOST_TEST(weather_parse_geolocation_fails_on_reported_failure) {
  // ipwho.is reports success: false (with no latitude/longitude) for a
  // query it cannot resolve, e.g. a reserved/private IP.
  constexpr char kGeoFailure[] = R"JSON({
    "success": false,
    "message": "reserved range"
  })JSON";

  double latitude = 1.0;
  double longitude = 2.0;
  const bool ok = weather::parse_geolocation_json(
      kGeoFailure, std::strlen(kGeoFailure), latitude, longitude);

  EXPECT_TRUE(!ok);
  // Untouched on failure.
  EXPECT_TRUE(latitude == 1.0 && longitude == 2.0);
}

HOST_TEST(weather_parse_geolocation_fails_on_malformed_json) {
  constexpr char kGarbage[] = "{not json";
  double latitude = 0.0;
  double longitude = 0.0;
  const bool ok = weather::parse_geolocation_json(kGarbage, std::strlen(kGarbage),
                                                    latitude, longitude);
  EXPECT_TRUE(!ok);
}
