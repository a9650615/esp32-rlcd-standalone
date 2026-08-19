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
// Before 09:00 on a weekday the slow interval is shortened to land the next
// refresh at the open plus kOpenWarmupSeconds, so a fetch at 08:55 no
// longer leaves yesterday's close on the panel until 09:25.
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

// How long after an open the first in-session refresh should land. Both
// markets wait it out: the Taiwan path shortens a pre-open sleep by the
// wall clock and the US path by epoch arithmetic, but the reason for not
// landing on the open itself is the same feed behaviour in both.
//
// Not zero, for a reason visible in a real response taken 12 minutes into
// a session: the intraday series is the session's completed 5-minute bars
// plus one partial bar at the current time. At the open itself that is a
// single point, fewer than market_parse.hpp's kMinIntradayPoints, so the
// page would draw no chart and print "CLOSE <today>" over "NO INTRADAY
// DATA" - a market that just opened, rendered as a closed one, which is
// the exact failure this whole path exists to stop. Five minutes in there
// are at least two points, which is a line.
inline constexpr int kOpenWarmupSeconds = 5 * 60;

// Seconds to sleep before the next US refresh.
//
// `now_epoch` is the device's current time in epoch seconds (0 when it has
// no trustworthy clock - the caller must not guess one), and
// `session_start` is market.hpp's us_session_start(), the exchange's own
// regular-session start from the last response, on that same scale.
//
// Both being epoch seconds is what makes this possible at all. The US
// market is the DST-observing timezone this component still has no data
// for, and it needs none: two absolute instants compare directly. There
// is no exchange calendar here either - a holiday's "start" is simply
// whatever the source last reported, and being wrong about it costs one
// refresh landing at an ordinary time.
//
// The result is only ever kRefreshIntervalSeconds (market.hpp) or *less
// than* it, and only for the one sleep that would otherwise step over the
// open: this moves when a refresh lands, it never adds one. Off-hours,
// mid-session and unknown-clock all return the flat interval this page
// has always used, so the request budget - and the radio time a
// battery-powered panel pays for it - is exactly what it was before.
//
// Without this the flat interval is free to straddle the open, leaving the
// page showing the previous session, correctly dated and complete, for up
// to a full interval after this one began. That reads as "the market has
// not opened yet", which was the original complaint.
int us_refresh_interval_seconds(long long now_epoch, long long session_start);

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
