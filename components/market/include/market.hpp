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

// These are indices glanced at on a wall panel, not a trading feed: neither
// the TWSE close (which itself only updates once a day - see the comment on
// parse_taiwan_index() in market_parse.hpp) nor "what's the S&P doing"
// needs anything close to real-time refresh. 30 minutes is at most 48
// requests/day per source: trivially light for the official, keyless TWSE
// endpoint, and - more importantly - respectful of the *unofficial* Yahoo
// endpoint, where aggressive polling is the surest way to get rate-limited
// or blocked outright.
//
// A flat interval is used whether the market is open or closed, instead of
// a market-hours-aware backoff. Doing that correctly needs a trading
// calendar (holidays, half-days, two timezones) this component has no
// access to; getting it wrong would silently reintroduce exactly the kind
// of confidently-wrong behavior this rewrite exists to remove, to save a
// request budget that is already trivial at 30 minutes.
inline constexpr int kRefreshIntervalSeconds = 30 * 60;

// Blocking. Fetches TWSE's MI_INDEX and updates the Taiwan cache. On any
// failure (network, HTTP status, malformed/truncated/wrong-shape JSON, a
// missing required field) the cache is set to invalid - never left at a
// stale prior value and never partially filled - so the next taiwan() call
// reports valid == false and the UI shows NO DATA. Returns true on success.
bool refresh_taiwan();

// Blocking. Fetches both S&P 500 and NASDAQ from the Yahoo-Finance-style
// chart endpoint (two requests: this source answers one symbol per call).
// Both must succeed and parse for the US cache to become valid; if either
// fails, the whole cache is set to invalid, matching refresh_taiwan()'s
// no-stale-data rule - a half-real, half-blank MarketData is not
// representable (there is one `valid` flag for the whole struct) and would
// not be honest anyway. Returns true on success.
bool refresh_us();

// Returns the current cached snapshot. No I/O; safe to call from any task,
// including the LVGL thread.
app_core::MarketData taiwan();
app_core::MarketData us();

}  // namespace market
