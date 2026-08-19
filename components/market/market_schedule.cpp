#include "market_schedule.hpp"

#include "market.hpp"

namespace market {
namespace {

// Days since 1970-01-01 for a proleptic Gregorian date - Howard Hinnant's
// well-known closed-form algorithm, the exact inverse direction of
// market_parse.cpp's civil_from_unix (epoch -> date instead of date ->
// epoch). Local to this file: it is a different calculation for a
// different purpose, not worth sharing across a component boundary for a
// few lines.
long long days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long long>(doe) - 719468;
}

// 0 = Sunday .. 6 = Saturday. 1970-01-01 (day 0) was a Thursday (4).
int weekday(const app_core::RtcDateTime& date) {
  const long long days =
      days_from_civil(date.year, date.month, date.day);
  return static_cast<int>(((days + 4) % 7 + 7) % 7);
}

constexpr int kSessionOpenMinute = 9 * 60;          // 09:00
constexpr int kSessionCloseMinute = 13 * 60 + 30;  // 13:30

}  // namespace

bool taiwan_market_hours(const app_core::RtcDateTime& local_time) {
  const int day = weekday(local_time);
  if (day == 0 || day == 6) return false;  // Sunday, Saturday.
  const int minute_of_day = local_time.hour * 60 + local_time.minute;
  return minute_of_day >= kSessionOpenMinute &&
         minute_of_day <= kSessionCloseMinute;
}

int taiwan_refresh_interval_seconds(const app_core::RtcDateTime& local_time,
                                    bool primary_active) {
  if (primary_active && taiwan_market_hours(local_time)) {
    return kTaiwanFastRefreshIntervalSeconds;
  }
  // Never sleep through the open. A refresh landing at 08:55 is still
  // off-hours, so it used to take the flat 30-minute interval and the panel
  // kept showing yesterday's close until 09:25 - the exact staleness the
  // fast in-session interval exists to prevent, just moved to the one
  // moment of the day it matters most. Minute granularity on purpose: the
  // seconds already elapsed in the current minute put the next refresh a
  // little after 09:00, not exactly on it, which is what Yahoo needs to
  // have published the session's first bar.
  const int day = weekday(local_time);
  if (day != 0 && day != 6) {
    const int minute_of_day = local_time.hour * 60 + local_time.minute;
    if (minute_of_day < kSessionOpenMinute) {
      const int seconds_until_open = (kSessionOpenMinute - minute_of_day) * 60;
      if (seconds_until_open < kRefreshIntervalSeconds) {
        return seconds_until_open;
      }
    }
  }
  return kRefreshIntervalSeconds;
}

}  // namespace market
