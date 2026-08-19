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

// Floor on the shortened pre-open sleep. A wake-up seconds from now buys
// nothing the next one would not, and a task that sleeps ~0 between two
// pairs of HTTPS requests is a battery drain and a way to get rate-limited.
constexpr int kMinSleepSeconds = 60;

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
  // moment of the day it matters most. Same fix as
  // us_refresh_interval_seconds() below and the same kOpenWarmupSeconds
  // wait, by a different route: Taiwan's session bounds are already in this
  // file, in a timezone with no DST, so this needs no epoch arithmetic and
  // no session metadata from a response that may not have arrived.
  //
  // Only ever shorter than kRefreshIntervalSeconds, and only for the one
  // sleep that would otherwise step over the open: this moves when a
  // refresh lands, it never adds one.
  const int day = weekday(local_time);
  if (day != 0 && day != 6) {
    const int minute_of_day = local_time.hour * 60 + local_time.minute;
    if (minute_of_day < kSessionOpenMinute) {
      const int until_warm =
          (kSessionOpenMinute - minute_of_day) * 60 + kOpenWarmupSeconds;
      if (until_warm < kRefreshIntervalSeconds) return until_warm;
    }
  }
  return kRefreshIntervalSeconds;
}

int us_refresh_interval_seconds(long long now_epoch, long long session_start) {
  // No clock, or a response that did not date its session: the flat
  // interval, which is what this page did before any of this existed.
  if (now_epoch <= 0 || session_start <= 0) return kRefreshIntervalSeconds;
  const long long until_warm =
      session_start + kOpenWarmupSeconds - now_epoch;
  // Already past the open (mid-session, or any time after the close, since
  // the source keeps reporting a session start until it rolls to the next
  // one) - nothing to align to.
  if (until_warm <= 0) return kRefreshIntervalSeconds;
  // Further out than one ordinary interval - including the whole of a
  // weekend or an overnight - is also nothing to align to yet. Sleeping
  // straight through to a distant open would mean trusting one number from
  // one response with hours of blackout; the ordinary interval gets there
  // in steps, and the last of those steps is the one that lands short.
  if (until_warm >= kRefreshIntervalSeconds) return kRefreshIntervalSeconds;
  return static_cast<int>(until_warm < kMinSleepSeconds ? kMinSleepSeconds
                                                        : until_warm);
}

}  // namespace market
