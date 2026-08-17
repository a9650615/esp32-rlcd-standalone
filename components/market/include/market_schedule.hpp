#pragma once

#include "app_snapshot.hpp"

// Pure "when to refresh Taiwan" policy - no I/O, no ESP-IDF, host-testable
// like market_parse.hpp. Kept separate from it: that file is about what a
// response means, this one is about how often to ask for one.
namespace market {

// True during Taiwan's regular cash session: weekdays, 09:00 through 13:30
// local time inclusive of both the open and the close minute. `local_time`
// is assumed to already be Asia/Taipei wall-clock time - this device's one
// fixed timezone (CST-8, no DST - see net_time_convert.hpp's kTimeZone),
// which is what net_time::now() already returns.
//
// No trading-holiday calendar: see taiwan_refresh_interval_seconds()'s own
// comment for why that gap is accepted rather than solved here.
bool taiwan_market_hours(const app_core::RtcDateTime& local_time);

// Seconds to sleep before the next Taiwan refresh.
//
// primary_active is whatever the most recent refresh_taiwan() reported
// (market_parse.hpp's TaiwanFetchOutcome::used_primary): true while Yahoo's
// ^TWII (near-real-time) served the data, false while running on the TWSE
// MI_INDEX fallback. MI_INDEX cannot change until after the close - asking
// again every few minutes during the session would just re-fetch an
// unchanged number - so the fallback always gets the slow interval,
// regardless of market hours.
//
// Otherwise: kTaiwanFastRefreshIntervalSeconds (market.hpp) while
// taiwan_market_hours() is true, kRefreshIntervalSeconds (market.hpp, the
// existing 30 min) everywhere else - off-hours, weekends, and the
// fallback. 09:00-13:30 is 270 minutes; at the fast interval that is 54
// requests on a trading day, well short of doubling the original flat
// 30-minute budget (48/day) once the off-hours slow requests are added
// back in.
//
// No trading-holiday calendar, the same trade-off market.hpp's comment on
// the flat interval already made: a market holiday reads as "open" here
// and simply spends the fast interval re-confirming an unchanged close -
// a handful of wasted requests, never a wrong number on the panel.
int taiwan_refresh_interval_seconds(const app_core::RtcDateTime& local_time,
                                    bool primary_active);

// No taiwan_session_elapsed_fraction() here (an earlier version had one,
// deriving it from these same session bounds and the board's own RTC).
// app_core::MarketData::session_elapsed_fraction is computed instead in
// market_parse.cpp's parse_yahoo_quote(), from Yahoo's own
// meta.currentTradingPeriod.regular.start/end and the response's own last
// timestamp - both epoch seconds, verified present in a real response
// before this was built. That needs no device clock, no timezone, and no
// DST arithmetic, which is why it now covers the US market too (a genuine
// device-clock version of this would need US Eastern time's DST rules,
// which this component still deliberately has no data for) - and it is
// more honest for Taiwan as well, since it reflects what the fetched
// series actually covers rather than what the wall clock says "should"
// have been fetched by now.
//
// This file keeps taiwan_market_hours()/taiwan_refresh_interval_seconds()
// above because they answer a different question the Yahoo metadata
// cannot: how soon to poll *again*, decided before that next response
// exists at all.

}  // namespace market
