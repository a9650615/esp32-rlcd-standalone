#pragma once

#include "app_snapshot.hpp"

#include <array>
#include <cstddef>
#include <string>

// Pure JSON-parsing core: no ESP-IDF, no network, no globals. Kept separate
// from market.hpp (the fetch/refresh layer) so it can be exercised by the
// host test suite without pulling in esp_http_client. Mirrors the split used
// by components/weather (weather_parse.hpp vs weather.hpp).
namespace market {

// One index quote (price + change), independently of which MarketData slot
// (primary/secondary) it ends up in. Yahoo's chart endpoint answers one
// symbol per request, so the US fetch layer issues two requests (S&P 500,
// NASDAQ) and combines two IndexQuote results into one MarketData; TWSE
// answers with the whole index table in a single call, so
// parse_taiwan_index() below fills a MarketData directly instead of going
// through this type.
struct IndexQuote {
  bool valid = false;
  std::string label;
  int value = 0;
  double change_percent = 0.0;
  // Real intraday close samples when the source supplied at least
  // kMinIntradayPoints (see parse_yahoo_quote); a flat repeat of `value`
  // otherwise. Never interpolated/invented points. Only the first
  // `sample_count` entries are meaningful - see that field.
  std::array<int, app_core::kIntradaySampleCount> samples{};
  // How many of the leading `samples` slots are real - may be fewer than
  // the array's own size early in a session (nothing pads the rest), or
  // exactly the array's size once enough raw points exist to fill it via
  // reduce_to_extremes(). 0 whenever has_intraday is false.
  uint8_t sample_count = 0;
  // The session these figures are from, from the source's own timestamp.
  // Zero when it did not supply one.
  uint16_t as_of_year = 0;
  uint8_t as_of_month = 0;
  uint8_t as_of_day = 0;
  // False when `samples` is a flat repeat of `value` rather than a real
  // series. The UI must not draw a chart in that case: the numbers are real
  // but the shape would not be.
  bool has_intraday = false;
  // See app_core::MarketData::session_elapsed_fraction's own comment for
  // the full reasoning - this is where it is actually computed, from this
  // response's own meta.currentTradingPeriod.regular.start/end and its
  // last timestamp. Default 1.0 (no shrink) whenever that metadata is
  // missing or the session is not actively in progress.
  float session_elapsed_fraction = 1.0f;
};

// Parses a TWSE /v1/exchangeReport/MI_INDEX response (a JSON array covering
// every index TWSE publishes, one row per index) into `out`, matching by
// name the two rows the Taiwan page needs: "發行量加權股價指數" (TAIEX,
// primary) and "臺灣50指數" (TW50, secondary). On any malformed or
// truncated body, or if either row is absent or missing a required field,
// returns false and resets `out` to a freshly default-constructed
// MarketData (valid == false). A missing field is never defaulted to zero -
// the whole parse fails instead.
//
// MI_INDEX is a once-daily closing snapshot ("每日收盤行情-大盤統計資訊"
// per its own swagger summary), not an intraday feed, so there is no real
// intraday series to report for it. Rather than leave intraday_samples at
// its zero default or invent points, it is filled with
// app_core::kIntradaySampleCount copies of the real TAIEX closing value:
// ui_data.hpp's normalize_chart_samples() renders any constant array as a
// flat horizontal line, so the chart on-device visibly reads as "no shape
// data" without ever displaying a number that was not real. has_intraday
// stays false, so nothing ever reads intraday_sample_count on this path.
bool parse_taiwan_index(const char* json, std::size_t length,
                         app_core::MarketData& out);

// Smallest number of real raw points that counts as an actual intraday
// series rather than a couple of dots - independent of
// app_core::kIntradaySampleCount, the chart's own *target* resolution: a
// session with fewer real bars than the target still deserves a chart (see
// parse_yahoo_quote's own comment on sample_count), it just is not
// downsampled. This is the one number actually being decided here, kept
// small and deliberately unrelated to how many pixels the chart has.
inline constexpr std::size_t kMinIntradayPoints = 8;

// Reduces `raw` (raw_count > out.size(), a precondition - the caller
// already knows to call this only once there are more raw points than
// output slots) to exactly out.size() points, one per contiguous bucket -
// bucket boundaries are the standard even i*raw_count/out.size() split, so
// every bucket gets at least one raw point. Each output slot is whichever
// raw point in its own bucket deviates most from the *previous* output
// slot's own chosen value - not the bucket's first, last, or middle point,
// and deliberately not the bucket's own mean either: a bucket of exactly
// two raw points (the common case once raw_count is only modestly above
// out.size(), e.g. this project's real ~79 US 5-minute bars into 64
// slots) has both points equidistant from their own two-point average by
// definition, which makes "deviates most from the bucket's own mean" an
// unbreakable tie that silently drops whichever point is not checked
// first. Comparing against the running series instead has no such blind
// spot: a spike is far from the flat value that precedes it regardless of
// what else shares its bucket.
//
// This is the fix for the actual complaint that motivated it: naive
// stride/index sampling (evenly picking one raw point per output slot,
// what this function replaces) drops whatever does not land on a kept
// index, including a real spike or dip - the shape ends up wrong, not just
// low-resolution.
void reduce_to_extremes(const double* raw, std::size_t raw_count,
                        std::array<int, app_core::kIntradaySampleCount>& out);

// Parses one Yahoo-Finance-style /v8/finance/chart/<symbol> response into a
// single IndexQuote. `display_label` is supplied by the caller (e.g.
// "S&P 500") rather than trusted from the response, since the caller
// already knows which symbol it requested.
//
// Required: chart.result[0].meta.regularMarketPrice and .previousClose,
// both numeric, with previousClose != 0. Any other shape - including the
// chart.error error-object response Yahoo returns for a bad/delisted symbol
// or when it declines the request - returns false with `out` reset to a
// default IndexQuote.
//
// chart.result[0].indicators.quote[0].close is read best-effort for the
// intraday series: fewer than kMinIntradayPoints usable points (missing,
// malformed, or genuinely too early in the session) leaves has_intraday
// false and fills `samples` with kIntradaySampleCount copies of the
// current price, same as before. Otherwise has_intraday is true and
// sample_count is set to whichever is smaller - the raw point count itself
// (copied through unchanged, one raw point per output slot, when there are
// not yet enough real bars to fill the target resolution) or
// kIntradaySampleCount (via reduce_to_extremes() above, once there are
// more raw points than that). Neither branch interpolates or invents a
// point; this does not fail the whole quote either way - price/change
// already came from `meta`, which is the only part treated as required.
//
// session_elapsed_fraction (see app_core::MarketData's own comment) is
// read best-effort from meta.currentTradingPeriod.regular.start/.end and
// the response's own last timestamp - all optional, all epoch seconds, no
// effect on whether the quote itself succeeds.
bool parse_yahoo_quote(const char* json, std::size_t length,
                        const std::string& display_label, IndexQuote& out);

// What refresh_taiwan() (market.hpp) ends up publishing, and whether the
// primary (Yahoo ^TWII) or the fallback (TWSE MI_INDEX) served it - see
// select_taiwan_source() below.
struct TaiwanFetchOutcome {
  bool ok = false;
  // True only when the primary actually served this refresh. False both
  // for a fallback success and for a total failure - callers that need to
  // tell those two apart already have `ok` for that.
  bool used_primary = false;
  app_core::MarketData data;
};

// Taiwan's fallback selection, as a pure decision with no I/O of its own -
// market.hpp's refresh_taiwan() does the actual fetching (Yahoo first,
// TWSE only when Yahoo has already failed - fetching both every cycle
// would double the request rate for a value normally discarded) and hands
// the two parse results here.
//
// primary_ok/primary is parse_yahoo_quote()'s result for ^TWII. When true,
// it wins outright: near-real-time data, published as-is, with no
// secondary index (Yahoo answers one symbol per request, and this source
// only asked for one - see render_market_sidebar() in
// components/ui/render_shared.cpp for how an empty secondary_label hides
// that tile instead of showing a fabricated "0 / +0.00%").
//
// fallback_ok/fallback is parse_taiwan_index()'s result for MI_INDEX -
// meaningful only when primary_ok is false, and already a fully-formed
// MarketData (TAIEX + TW50, no intraday - MI_INDEX is a once-daily
// closing snapshot, not a live feed) that this function simply passes
// through unchanged.
//
// Both failing publishes nothing rather than stale or partial data,
// matching every other provider's rule: a half-real MarketData is not
// representable (one `valid` flag for the whole struct) and would not be
// honest anyway.
//
// A fallback success still counts as `ok` - the alternative (only the
// primary counts as a real refresh) would leave a Yahoo outage stuck at
// the fast retry interval forever, hammering an endpoint that is down for
// a reason unrelated to how often it is asked.
TaiwanFetchOutcome select_taiwan_source(bool primary_ok,
                                        const IndexQuote& primary,
                                        bool fallback_ok,
                                        const app_core::MarketData& fallback);

}  // namespace market
