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
  // Real intraday close samples when the source supplied at least 8; a flat
  // repeat of `value` otherwise (see parse_yahoo_quote below). Never
  // interpolated/invented points.
  std::array<int, 8> samples{};
  // The session these figures are from, from the source's own timestamp.
  // Zero when it did not supply one.
  uint16_t as_of_year = 0;
  uint8_t as_of_month = 0;
  uint8_t as_of_day = 0;
  // False when `samples` is a flat repeat of `value` rather than a real
  // series. The UI must not draw a chart in that case: the numbers are real
  // but the shape would not be.
  bool has_intraday = false;
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
// its zero default or invent points, it is filled with 8 copies of the real
// TAIEX closing value: ui_data.hpp's normalize_chart_samples() renders any
// constant array as a flat horizontal line, so the chart on-device visibly
// reads as "no shape data" without ever displaying a number that was not
// real.
bool parse_taiwan_index(const char* json, std::size_t length,
                         app_core::MarketData& out);

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
// intraday series: when it supplies at least 8 numeric points they are
// evenly downsampled to exactly 8; otherwise (missing, malformed, or fewer
// than 8 usable points - e.g. shortly after the US market opens, or a
// symbol change dropping the block entirely) `samples` is filled with 8
// copies of the current price instead of interpolating or inventing
// points. This does not fail the whole quote: price/change already came
// from `meta`, which is the only part treated as required.
bool parse_yahoo_quote(const char* json, std::size_t length,
                        const std::string& display_label, IndexQuote& out);

}  // namespace market
