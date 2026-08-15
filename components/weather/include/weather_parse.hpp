#pragma once

#include "app_snapshot.hpp"

#include <cstddef>

// Pure JSON-parsing core: no ESP-IDF, no network, no globals. Kept separate
// from weather.hpp (the fetch/cache layer) so it can be exercised by the
// host test suite without pulling in esp_http_client.
namespace weather {

// Maps an Open-Meteo WMO weather_code (open-meteo.com/en/docs, "WMO Weather
// interpretation codes" table) to a short ASCII condition string. The
// compiled Montserrat glyph subset used on the panel has no icon glyphs and
// lacks most punctuation, so these are plain words, no icons, minimal
// punctuation, short enough for a 400x300 monochrome layout. Unknown codes
// map to "Unknown" rather than guessing.
const char* condition_for_wmo_code(int code);

// Parses an Open-Meteo `/v1/forecast` response (current + 7-day daily
// blocks) into `out`. On any malformed, truncated, or partial input -
// including a single missing required field - returns false and leaves
// `out` a freshly default-constructed WeatherData (valid == false). A
// missing field is never defaulted to zero; the whole parse fails instead.
bool parse_forecast_json(const char* json, std::size_t length,
                          app_core::WeatherData& out);

// Parses an ipwho.is IP-geolocation response. Returns true and fills
// latitude/longitude only when the response reports "success": true and
// carries numeric latitude/longitude fields; otherwise returns false and
// leaves latitude/longitude untouched.
bool parse_geolocation_json(const char* json, std::size_t length,
                             double& latitude, double& longitude);

}  // namespace weather
