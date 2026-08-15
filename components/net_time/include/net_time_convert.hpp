#pragma once

#include <ctime>

#include "app_snapshot.hpp"

// Pure epoch -> local-time conversion, no ESP dependency. Split out from
// net_time.hpp (which pulls in esp_err.h) so host tests can link this half
// without the ESP-IDF headers the SNTP half needs.
namespace net_time {

// POSIX TZ rule for Taiwan: fixed UTC+8, no DST, no zoneinfo database
// lookup required. This is the single place the +8 offset is spelled out;
// everything else gets local time via localtime_r() once this is active.
// Device side: start() below calls setenv("TZ", kTimeZone, 1) + tzset().
// Host tests: do the same before calling epoch_to_local().
inline constexpr const char* kTimeZone = "CST-8";

// Breaks a UTC epoch down into Taiwan local time using the active TZ.
void epoch_to_local(std::time_t epoch, app_core::RtcDateTime& out);

}  // namespace net_time
