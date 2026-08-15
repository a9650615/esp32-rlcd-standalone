#pragma once

#include "app_snapshot.hpp"
#include "weather_parse.hpp"

// Fetch + cache layer on top of weather_parse.hpp. This header pulls in
// ESP-IDF (via weather.cpp) and is not part of the host test build; only
// weather_parse.hpp/.cpp are host-tested.
//
// This component does not run its own task and does not publish into
// AppSnapshot - both are main/app_core's job. A caller there is expected to
// invoke refresh() periodically (see kRefreshIntervalSeconds) from whatever
// task already owns network I/O, and to copy current() into
// AppSnapshot::weather / new_york_weather on the LVGL thread.
namespace weather {

enum class LocationSource { IpGeolocation, Manual };

// Weather changes slowly enough that a wall panel a person glances at does
// not need anything close to real-time updates; 30 minutes keeps the panel
// reasonably fresh while staying well inside Open-Meteo's fair-use
// expectations for a keyless client and ipwho.is's 1000 requests/day quota
// (this refresh also re-resolves IP geolocation - see resolve_ip_location
// in weather.cpp - so each cycle costs one request to each service, not
// just one).
inline constexpr int kRefreshIntervalSeconds = 30 * 60;

// Two missed refresh cycles (60 minutes) before a reading is shown as
// stale, so a single transient Wi-Fi/API hiccup doesn't flip the page to
// "stale" on every ordinary retry - it takes a sustained outage.
inline constexpr int kStaleAfterSeconds = 2 * kRefreshIntervalSeconds;

// Blocking. Resolves location (IP geolocation, unless a manual override is
// set - see set_manual_location), fetches current conditions and the 7-day
// forecast from Open-Meteo, and updates the cache on success. On any
// failure (network, HTTP status, malformed JSON) the previously cached
// reading, if any, is left untouched; current() marks it stale once
// kStaleAfterSeconds has elapsed. Returns true on a successful refresh.
bool refresh();

// Returns the cached snapshot with `stale` recomputed against wall-clock
// time. No I/O; safe to call from any task, including the LVGL thread.
app_core::WeatherData current();

// Manual override: latitude/longitude supplied here wins over IP
// geolocation starting with the next refresh(). IP geolocation is routinely
// off by tens of kilometres (it resolves to an ISP facility, not the
// device), so this is the escape hatch, not optional polish.
void set_manual_location(double latitude, double longitude);

// Reverts to IP geolocation starting with the next refresh().
void use_ip_location();

LocationSource location_source();

}  // namespace weather
