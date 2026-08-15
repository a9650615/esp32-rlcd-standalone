#include "weather.hpp"

#include <cstdio>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace weather {
namespace {

constexpr char kTag[] = "weather";
constexpr int kHttpTimeoutMs = 8000;
// Bounded response buffers: an unbounded read of a remote body into a heap
// allocation is a defect even with 8 MB of PSRAM to spare. Both bodies are a
// few KB in practice (7 forecast days x 5 numeric fields, one JSON object
// for geolocation); these caps are generous multiples of that with room for
// formatting/whitespace, not "as much as fits".
constexpr int kForecastBufferBytes = 8192;
constexpr int kGeolocationBufferBytes = 2048;

app_core::WeatherData g_cache;  // valid == false until the first success.
int64_t g_last_success_us = 0;

LocationSource g_source = LocationSource::IpGeolocation;
double g_latitude = 0.0;
double g_longitude = 0.0;
bool g_have_location = false;

// GETs `url` into `buffer` (capacity `buffer_size`, including the
// terminating NUL), null-terminates it, and reports the byte count read.
// Every esp_err_t and HTTP status is logged; callers only see success/fail.
esp_err_t http_get(const char* url, char* buffer, int buffer_size,
                    int& out_len) {
  esp_http_client_config_t config = {};
  config.url = url;
  config.timeout_ms = kHttpTimeoutMs;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    ESP_LOGE(kTag, "esp_http_client_init failed for %s", url);
    return ESP_FAIL;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "open failed for %s: %s", url, esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return err;
  }

  const int64_t content_length = esp_http_client_fetch_headers(client);
  if (content_length < -1) {
    ESP_LOGW(kTag, "fetch_headers failed for %s", url);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }

  const int read = esp_http_client_read_response(client, buffer,
                                                   buffer_size - 1);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (read < 0) {
    ESP_LOGW(kTag, "read failed for %s", url);
    return ESP_FAIL;
  }
  buffer[read] = '\0';
  out_len = read;

  if (status != 200) {
    ESP_LOGW(kTag, "%s returned HTTP %d", url, status);
    return ESP_FAIL;
  }
  return ESP_OK;
}

// IP geolocation via ipwho.is: no API key, HTTPS on the free tier (unlike
// ip-api.com, whose free tier is HTTP-only and so cannot go through this
// component's certificate-bundle-verified fetch path), 1000 requests/day
// (see https://ipwhois.io/documentation - checked against current docs).
// An empty path queries the caller's own public IP.
//
// This sends the device's public IP address to ipwho.is (a third party) in
// order to derive an approximate location. It is the default; a manual
// override (set_manual_location) avoids it entirely.
bool resolve_ip_location(double& latitude, double& longitude) {
  char buffer[kGeolocationBufferBytes];
  int len = 0;
  if (http_get("https://ipwho.is/", buffer, sizeof(buffer), len) != ESP_OK) {
    return false;
  }
  return parse_geolocation_json(buffer, len, latitude, longitude);
}

}  // namespace

bool refresh() {
  double latitude = g_latitude;
  double longitude = g_longitude;

  if (g_source == LocationSource::IpGeolocation) {
    double resolved_lat = 0.0;
    double resolved_lon = 0.0;
    if (resolve_ip_location(resolved_lat, resolved_lon)) {
      latitude = resolved_lat;
      longitude = resolved_lon;
      g_latitude = latitude;
      g_longitude = longitude;
      g_have_location = true;
    } else {
      ESP_LOGW(kTag, "IP geolocation failed; %s",
               g_have_location ? "reusing last known location"
                                : "no location available yet");
    }
  }

  if (!g_have_location) return false;

  // Open-Meteo docs (open-meteo.com/en/docs, "Forecast APIs / Hourly
  // Weather Variables" and "Daily Weather Variables" sections): current
  // has no precipitation-probability field (see the comment in
  // weather_parse.cpp), so `current` carries only temperature + weather
  // code and the daily block supplies the 7-day forecast plus today's rain
  // probability. `timezone=auto` derives the local day boundaries from
  // lat/lon so daily.time lines up with the location's own calendar days.
  char url[256];
  std::snprintf(
      url, sizeof(url),
      "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
      "&current=temperature_2m,weather_code"
      "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
      "precipitation_probability_max"
      "&timezone=auto&forecast_days=7",
      latitude, longitude);

  char buffer[kForecastBufferBytes];
  int len = 0;
  if (http_get(url, buffer, sizeof(buffer), len) != ESP_OK) return false;

  app_core::WeatherData parsed;
  if (!parse_forecast_json(buffer, len, parsed)) {
    ESP_LOGW(kTag, "forecast JSON did not parse");
    return false;
  }

  g_cache = parsed;
  g_last_success_us = esp_timer_get_time();
  return true;
}

app_core::WeatherData current() {
  app_core::WeatherData snapshot = g_cache;
  if (snapshot.valid) {
    const int64_t elapsed_s =
        (esp_timer_get_time() - g_last_success_us) / 1000000;
    snapshot.stale = elapsed_s > kStaleAfterSeconds;
  }
  return snapshot;
}

void set_manual_location(double latitude, double longitude) {
  g_latitude = latitude;
  g_longitude = longitude;
  g_have_location = true;
  g_source = LocationSource::Manual;
}

void use_ip_location() {
  g_source = LocationSource::IpGeolocation;
  g_have_location = false;
}

LocationSource location_source() { return g_source; }

}  // namespace weather
