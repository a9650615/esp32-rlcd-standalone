#include "weather_parse.hpp"

#include "cJSON.h"

#include <cstdio>

namespace weather {
namespace {

// Sakamoto's algorithm: weekday of a Gregorian calendar date, 0 = Sunday.
// Open-Meteo's daily.time entries are ISO dates ("2026-08-16") with no
// weekday name attached, so this is the cheapest way to get one.
const char* weekday_abbrev(int year, int month, int day) {
  static const int kMonthTable[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  static const char* kNames[] = {"Sun", "Mon", "Tue", "Wed",
                                  "Thu", "Fri", "Sat"};
  int y = year;
  if (month < 3) y -= 1;
  const int w =
      (y + y / 4 - y / 100 + y / 400 + kMonthTable[month - 1] + day) % 7;
  return kNames[(w + 7) % 7];
}

uint8_t clamp_percent(double value) {
  if (value < 0.0) return 0;
  if (value > 100.0) return 100;
  return static_cast<uint8_t>(value + 0.5);
}

}  // namespace

const char* condition_for_wmo_code(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly Clear";
    case 2: return "Partly Cloudy";
    case 3: return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 56:
    case 57: return "Icy Drizzle";
    case 61:
    case 63:
    case 65: return "Rain";
    case 66:
    case 67: return "Icy Rain";
    case 71:
    case 73:
    case 75: return "Snow";
    case 77: return "Snow Grains";
    case 80:
    case 81:
    case 82: return "Showers";
    case 85:
    case 86: return "Snow Showers";
    case 95: return "Thunderstorm";
    case 96:
    case 99: return "Tstorm Hail";
    default: return "Unknown";
  }
}

bool parse_forecast_json(const char* json, std::size_t length,
                          app_core::WeatherData& out) {
  out = app_core::WeatherData{};

  cJSON* root = cJSON_ParseWithLength(json, length);
  if (root == nullptr) return false;

  bool ok = false;
  do {
    cJSON* current = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (!cJSON_IsObject(current)) break;
    cJSON* temperature =
        cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
    cJSON* current_code =
        cJSON_GetObjectItemCaseSensitive(current, "weather_code");
    if (!cJSON_IsNumber(temperature) || !cJSON_IsNumber(current_code)) break;

    cJSON* daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (!cJSON_IsObject(daily)) break;
    cJSON* time = cJSON_GetObjectItemCaseSensitive(daily, "time");
    cJSON* codes = cJSON_GetObjectItemCaseSensitive(daily, "weather_code");
    cJSON* highs =
        cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max");
    cJSON* lows =
        cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min");
    cJSON* rain_probs = cJSON_GetObjectItemCaseSensitive(
        daily, "precipitation_probability_max");
    if (!cJSON_IsArray(time) || !cJSON_IsArray(codes) ||
        !cJSON_IsArray(highs) || !cJSON_IsArray(lows) ||
        !cJSON_IsArray(rain_probs)) {
      break;
    }

    // Matches app_core::WeatherData::seven_day's std::array<WeatherDay, 7>.
    constexpr int kDays = 7;
    if (cJSON_GetArraySize(time) < kDays ||
        cJSON_GetArraySize(codes) < kDays ||
        cJSON_GetArraySize(highs) < kDays ||
        cJSON_GetArraySize(lows) < kDays ||
        cJSON_GetArraySize(rain_probs) < kDays) {
      break;
    }

    // Open-Meteo has no "current" precipitation-probability field (it is
    // only exposed hourly/daily); today's daily max is the closest
    // available proxy for "chance of rain right now".
    cJSON* today_rain_prob = cJSON_GetArrayItem(rain_probs, 0);
    if (!cJSON_IsNumber(today_rain_prob)) break;

    app_core::WeatherData parsed;
    parsed.current.temperature_c = temperature->valuedouble;
    parsed.current.condition = condition_for_wmo_code(
        static_cast<int>(current_code->valuedouble));
    parsed.current.rain_probability_percent =
        clamp_percent(today_rain_prob->valuedouble);

    bool days_ok = true;
    for (int i = 0; i < kDays; ++i) {
      cJSON* date_item = cJSON_GetArrayItem(time, i);
      cJSON* code_item = cJSON_GetArrayItem(codes, i);
      cJSON* high_item = cJSON_GetArrayItem(highs, i);
      cJSON* low_item = cJSON_GetArrayItem(lows, i);
      cJSON* rain_item = cJSON_GetArrayItem(rain_probs, i);
      if (!cJSON_IsString(date_item) || !cJSON_IsNumber(code_item) ||
          !cJSON_IsNumber(high_item) || !cJSON_IsNumber(low_item) ||
          !cJSON_IsNumber(rain_item)) {
        days_ok = false;
        break;
      }
      int year = 0, month = 0, day = 0;
      if (std::sscanf(date_item->valuestring, "%d-%d-%d", &year, &month,
                       &day) != 3 ||
          month < 1 || month > 12 || day < 1 || day > 31) {
        days_ok = false;
        break;
      }
      app_core::WeatherDay& slot = parsed.seven_day[i];
      slot.day = weekday_abbrev(year, month, day);
      slot.condition =
          condition_for_wmo_code(static_cast<int>(code_item->valuedouble));
      slot.high_c = high_item->valuedouble;
      slot.low_c = low_item->valuedouble;
      slot.rain_probability_percent = clamp_percent(rain_item->valuedouble);
    }
    if (!days_ok) break;

    parsed.valid = true;
    parsed.stale = false;
    out = parsed;
    ok = true;
  } while (false);

  cJSON_Delete(root);
  return ok;
}

bool parse_geolocation_json(const char* json, std::size_t length,
                             double& latitude, double& longitude) {
  cJSON* root = cJSON_ParseWithLength(json, length);
  if (root == nullptr) return false;

  bool ok = false;
  cJSON* success = cJSON_GetObjectItemCaseSensitive(root, "success");
  cJSON* lat_item = cJSON_GetObjectItemCaseSensitive(root, "latitude");
  cJSON* lon_item = cJSON_GetObjectItemCaseSensitive(root, "longitude");
  if (cJSON_IsBool(success) && cJSON_IsTrue(success) &&
      cJSON_IsNumber(lat_item) && cJSON_IsNumber(lon_item)) {
    latitude = lat_item->valuedouble;
    longitude = lon_item->valuedouble;
    ok = true;
  }

  cJSON_Delete(root);
  return ok;
}

}  // namespace weather
