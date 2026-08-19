#pragma once

#include "app_snapshot.hpp"
#include "market_parse.hpp"

// Fetch + refresh-policy layer on top of market_parse.hpp. This header pulls
// in ESP-IDF (via market.cpp) and is not part of the host test build; only
// market_parse.hpp/.cpp are host-tested. Mirrors components/weather's
// weather.hpp/weather_parse.hpp split.
//
// This component does not run its own task and does not publish into
// AppSnapshot - both are main/app_core's job. A caller there is expected to
// call refresh_taiwan()/refresh_us() periodically (see
// kRefreshIntervalSeconds) from whatever task already owns network I/O, and
// to copy taiwan()/us() into AppSnapshot::taiwan_market / us_market on the
// LVGL thread.
namespace market {

// The baseline, flat interval - still what US uses unconditionally, and
// what Taiwan falls back to outside its trading hours or while running on
// the TWSE fallback (see kTaiwanFastRefreshIntervalSeconds and
// market_schedule.hpp below). 30 minutes is at most 48 requests/day per
// source: trivially light for the official, keyless TWSE endpoint, and -
// more importantly - respectful of the *unofficial* Yahoo endpoint, where
// aggressive polling is the surest way to get rate-limited or blocked
// outright.
//
// This used to be the only interval, flat whether a market was open or
// closed. It is still the interval both markets use most of the time, US
// included: the US page is not polled any faster during its session than
// outside it (see market_schedule.hpp's us_refresh_interval_seconds(),
// which only moves *when* a refresh lands, never how many there are).
// Taiwan no longer does: the
// operator hit the flat interval's actual cost directly - the market open
// at 09:00 with the page still showing Friday's close - and a trading
// calendar was never the missing piece for that, market hours were. See
// market_schedule.hpp's taiwan_refresh_interval_seconds() for the
// market-hours-aware decision and its own arithmetic.
inline constexpr int kRefreshIntervalSeconds = 30 * 60;

// Taiwan's regular session, while the primary (Yahoo ^TWII) source is
// serving the data - see market_schedule.hpp's taiwan_refresh_interval_seconds()
// for exactly when this applies versus kRefreshIntervalSeconds above.
inline constexpr int kTaiwanFastRefreshIntervalSeconds = 5 * 60;

// Blocking. Tries Yahoo's ^TWII chart endpoint first (near-real-time,
// unofficial); only on that source's failure does it fall back to TWSE's
// official, keyless MI_INDEX (a once-daily closing snapshot - see the
// comment on parse_taiwan_index() in market_parse.hpp). Never fetches
// both in the same call: the fallback is only reached once the primary has
// already failed, so a healthy primary costs exactly one request, not two.
// See market_parse.hpp's select_taiwan_source() for the actual decision
// (a pure function, host-tested there) and taiwan_using_primary_source()
// below for which source served the current cache.
//
// On total failure (both sources) the cache is set to invalid - never left
// at a stale prior value and never partially filled - so the next
// taiwan() call reports valid == false and the UI shows NO DATA. Returns
// true whenever either source succeeded, fallback included - see
// select_taiwan_source()'s own comment for why a fallback success counts
// as a real refresh for the caller's retry/interval bookkeeping.
bool refresh_taiwan();

// True if the most recently completed refresh_taiwan() was served by the
// primary (Yahoo ^TWII); false if it fell back to TWSE, or if the last
// call failed outright (in which case taiwan().valid is also false).
// market_schedule.hpp's taiwan_refresh_interval_seconds() needs this to
// decide whether the fast interval is actually buying anything.
bool taiwan_using_primary_source();

// Blocking. Fetches both S&P 500 and NASDAQ from the Yahoo-Finance-style
// chart endpoint (two requests: this source answers one symbol per call).
// Both must succeed and parse for the US cache to become valid; if either
// fails, the whole cache is set to invalid, matching refresh_taiwan()'s
// no-stale-data rule - a half-real, half-blank MarketData is not
// representable (there is one `valid` flag for the whole struct) and would
// not be honest anyway. Returns true on success.
bool refresh_us();

// meta.currentTradingPeriod.regular.start from the last successful
// refresh_us() - the epoch second the exchange itself said its regular
// session begins - or 0 when the last call failed or the response did not
// carry it. market_schedule.hpp's us_refresh_interval_seconds() needs it
// to land a refresh just after the open instead of up to a full interval
// past it. Mirrors taiwan_using_primary_source() above: refresh state the
// scheduler needs and the screen does not, so it stays out of MarketData.
long long us_session_start();

// Returns the current cached snapshot. No I/O; safe to call from any task,
// including the LVGL thread.
app_core::MarketData taiwan();
app_core::MarketData us();

}  // namespace market
