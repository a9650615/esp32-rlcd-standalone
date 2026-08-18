#include "market_parse.hpp"

#include "cJSON.h"

#include <cmath>
#include <cstdlib>

namespace market {
namespace {

// Days-to-civil, Howard Hinnant's algorithm. UTC rather than the exchange's
// own zone: the response gives seconds since the epoch and the timezone name
// separately, and quietly applying one to the other would turn a reported fact
// into a computed guess. A close is dated by its UTC day here, which is the
// same calendar day as New York's for any regular session.
void civil_from_unix(long long seconds, uint16_t& year, uint8_t& month,
                     uint8_t& day) {
  long long z = seconds / 86400 + 719468;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned long long doe = static_cast<unsigned long long>(z - era * 146097);
  const unsigned long long yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long long y = static_cast<long long>(yoe) + era * 400;
  const unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned long long mp = (5 * doy + 2) / 153;
  const unsigned long long d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned long long m = mp + (mp < 10 ? 3 : -9);
  year = static_cast<uint16_t>(y + (m <= 2 ? 1 : 0));
  month = static_cast<uint8_t>(m);
  day = static_cast<uint8_t>(d);
}

}  // namespace

namespace {

// Field values TWSE publishes for MI_INDEX are all JSON strings (confirmed
// against the endpoint's own swagger schema, e.g. "收盤指數": {"type":
// "string"}), not numbers - this extracts one and fails closed on anything
// else (missing key, null, non-string, empty).
bool string_field(const cJSON* object, const char* key, std::string& out) {
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) return false;
  out = item->valuestring;
  return true;
}

// Parses a TWSE numeric-string field (e.g. "45811.01", "-0.46") with
// strtod, rejecting anything that doesn't fully consume as a number -
// leftover garbage after the number, or an empty string, both fail rather
// than silently truncating.
bool numeric_string_field(const cJSON* object, const char* key, double& out) {
  std::string text;
  if (!string_field(object, key, text) || text.empty()) return false;
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end != text.c_str() + text.size()) return false;
  out = value;
  return true;
}

}  // namespace

// See this function's own doc comment in market_parse.hpp for why it
// exists (naive stride sampling drops a spike; this does not) and its
// precondition (raw_count > out.size()). External linkage (not in the
// anonymous namespace above) on purpose: declared in market_parse.hpp so
// the host test suite can exercise it directly.
void reduce_to_extremes(const double* raw, std::size_t raw_count,
                        std::array<int, app_core::kIntradaySampleCount>& out) {
  const std::size_t n = out.size();
  // Deviation from the *previous bucket's own chosen value*, not from
  // this bucket's own mean: for a bucket of exactly two raw points (the
  // common case once raw_count is only modestly above n, e.g. the real
  // ~79 US bars into 64 slots), both points are by definition equidistant
  // from their own two-point average - "deviation from bucket mean" is an
  // unbreakable tie there, which silently drops whichever candidate does
  // not happen to be checked first. Comparing against the running series
  // instead - literally "furthest from the neighbour that precedes it" -
  // has no such blind spot: a spike is far from the flat value before it
  // regardless of what shares its own bucket.
  double previous = raw[0];
  for (std::size_t i = 0; i < n; ++i) {
    std::size_t start = (i * raw_count) / n;
    std::size_t end = ((i + 1) * raw_count) / n;
    if (end <= start) end = start + 1;
    if (end > raw_count) end = raw_count;

    double best_value = raw[start];
    double best_deviation = -1.0;
    for (std::size_t j = start; j < end; ++j) {
      const double deviation = std::fabs(raw[j] - previous);
      if (deviation > best_deviation) {
        best_deviation = deviation;
        best_value = raw[j];
      }
    }
    out[i] = static_cast<int>(std::lround(best_value));
    previous = best_value;
  }
}

bool parse_taiwan_index(const char* json, std::size_t length,
                         app_core::MarketData& out) {
  out = app_core::MarketData{};

  cJSON* root = cJSON_ParseWithLength(json, length);
  if (root == nullptr) return false;

  bool ok = false;
  do {
    if (!cJSON_IsArray(root)) break;  // unexpected shape: not a row list.

    double taiex_value = 0.0, taiex_change = 0.0;
    double tw50_value = 0.0, tw50_change = 0.0;
    bool have_taiex = false, have_tw50 = false;
    uint16_t as_of_year = 0;
    uint8_t as_of_month = 0;
    uint8_t as_of_day = 0;

    const cJSON* row = nullptr;
    cJSON_ArrayForEach(row, root) {
      if (!cJSON_IsObject(row)) continue;
      std::string name;
      if (!string_field(row, "指數", name)) continue;
      // "1150814" is ROC year 115, month 08, day 14 - the Republic-of-China
      // calendar TWSE publishes in, 1911 years behind the Gregorian one.
      std::string roc_date;
      if (as_of_year == 0 && string_field(row, "日期", roc_date) &&
          roc_date.size() == 7) {
        const int roc = (roc_date[0] - '0') * 100 + (roc_date[1] - '0') * 10 +
                        (roc_date[2] - '0');
        const int month = (roc_date[3] - '0') * 10 + (roc_date[4] - '0');
        const int day = (roc_date[5] - '0') * 10 + (roc_date[6] - '0');
        if (roc > 0 && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
          as_of_year = static_cast<uint16_t>(roc + 1911);
          as_of_month = static_cast<uint8_t>(month);
          as_of_day = static_cast<uint8_t>(day);
        }
      }
      if (name == "發行量加權股價指數" && !have_taiex) {
        have_taiex = numeric_string_field(row, "收盤指數", taiex_value) &&
                     numeric_string_field(row, "漲跌百分比", taiex_change);
      } else if (name == "臺灣50指數" && !have_tw50) {
        have_tw50 = numeric_string_field(row, "收盤指數", tw50_value) &&
                    numeric_string_field(row, "漲跌百分比", tw50_change);
      }
    }
    if (!have_taiex || !have_tw50) break;

    app_core::MarketData parsed;
    parsed.display_name = "TAIWAN MARKET";
    parsed.primary_label = "TAIEX";
    parsed.primary_value = static_cast<int>(std::lround(taiex_value));
    parsed.primary_change_percent = taiex_change;
    parsed.secondary_label = "TW50";
    parsed.secondary_value = static_cast<int>(std::lround(tw50_value));
    parsed.secondary_change_percent = tw50_change;
    parsed.as_of_year = as_of_year;
    parsed.as_of_month = as_of_month;
    parsed.as_of_day = as_of_day;
    // No intraday feed in this response - see the comment on
    // parse_taiwan_index() in market_parse.hpp for why a flat repeat of the
    // real close, not zero or an interpolated series, is used here.
    parsed.intraday_samples.fill(parsed.primary_value);
    parsed.valid = true;

    out = parsed;
    ok = true;
  } while (false);

  cJSON_Delete(root);
  return ok;
}

bool parse_yahoo_quote(const char* json, std::size_t length,
                        const std::string& display_label, IndexQuote& out) {
  out = IndexQuote{};

  cJSON* root = cJSON_ParseWithLength(json, length);
  if (root == nullptr) return false;

  bool ok = false;
  do {
    const cJSON* chart = cJSON_GetObjectItemCaseSensitive(root, "chart");
    if (!cJSON_IsObject(chart)) break;  // unexpected shape.

    // Yahoo's documented failure response for a bad/delisted symbol or a
    // declined request is {"chart":{"result":null,"error":{...}}} - treat
    // any non-null error object as authoritative failure regardless of
    // what `result` looks like.
    const cJSON* error = cJSON_GetObjectItemCaseSensitive(chart, "error");
    if (error != nullptr && !cJSON_IsNull(error)) break;

    const cJSON* results = cJSON_GetObjectItemCaseSensitive(chart, "result");
    if (!cJSON_IsArray(results) || cJSON_GetArraySize(results) < 1) break;
    const cJSON* result0 = cJSON_GetArrayItem(results, 0);
    if (!cJSON_IsObject(result0)) break;

    const cJSON* meta = cJSON_GetObjectItemCaseSensitive(result0, "meta");
    if (!cJSON_IsObject(meta)) break;
    uint16_t as_of_year = 0;
    uint8_t as_of_month = 0;
    uint8_t as_of_day = 0;
    const cJSON* market_time =
        cJSON_GetObjectItemCaseSensitive(meta, "regularMarketTime");
    if (cJSON_IsNumber(market_time) && market_time->valuedouble > 0) {
      civil_from_unix(static_cast<long long>(market_time->valuedouble),
                      as_of_year, as_of_month, as_of_day);
    }
    const cJSON* price_item =
        cJSON_GetObjectItemCaseSensitive(meta, "regularMarketPrice");
    const cJSON* prev_item =
        cJSON_GetObjectItemCaseSensitive(meta, "previousClose");
    if (!cJSON_IsNumber(price_item) || !cJSON_IsNumber(prev_item)) break;
    const double price = price_item->valuedouble;
    const double previous_close = prev_item->valuedouble;
    if (previous_close == 0.0) break;  // would divide by zero below.

    // Intraday series: best-effort. Missing/malformed/short does not fail
    // the quote - price/change above already satisfied the required part.
    std::array<int, app_core::kIntradaySampleCount> samples{};
    uint8_t sample_count = 0;
    bool have_intraday = false;
    const cJSON* indicators =
        cJSON_GetObjectItemCaseSensitive(result0, "indicators");
    const cJSON* quotes = cJSON_IsObject(indicators)
                               ? cJSON_GetObjectItemCaseSensitive(indicators,
                                                                   "quote")
                               : nullptr;
    const cJSON* quote0 = (cJSON_IsArray(quotes) &&
                            cJSON_GetArraySize(quotes) >= 1)
                              ? cJSON_GetArrayItem(quotes, 0)
                              : nullptr;
    const cJSON* closes = cJSON_IsObject(quote0)
                               ? cJSON_GetObjectItemCaseSensitive(quote0,
                                                                   "close")
                               : nullptr;
    if (cJSON_IsArray(closes)) {
      // Bounded scratch buffer, not a heap vector: comfortably above the
      // most raw bars any (symbol, interval, range) this component
      // actually requests can produce - a hostile/garbled response is
      // simply capped rather than chased. This is scratch space for the
      // *raw* series; app_core::kIntradaySampleCount is the unrelated,
      // much smaller *output* resolution target.
      constexpr std::size_t kMaxRawPoints = 128;
      std::array<double, kMaxRawPoints> valid_closes{};
      std::size_t valid_count = 0;
      const cJSON* point = nullptr;
      cJSON_ArrayForEach(point, closes) {
        if (valid_count >= kMaxRawPoints) break;
        if (cJSON_IsNumber(point)) valid_closes[valid_count++] = point->valuedouble;
      }
      if (valid_count >= kMinIntradayPoints) {
        if (valid_count <= samples.size()) {
          // Fewer real bars than the chart's own resolution target -
          // nothing to reduce, and nothing to pad the remaining slots
          // with either. One raw point per output slot, in order.
          for (std::size_t i = 0; i < valid_count; ++i) {
            samples[i] = static_cast<int>(std::lround(valid_closes[i]));
          }
          sample_count = static_cast<uint8_t>(valid_count);
        } else {
          reduce_to_extremes(valid_closes.data(), valid_count, samples);
          sample_count = static_cast<uint8_t>(samples.size());
        }
        have_intraday = true;
      }
    }
    if (!have_intraday) {
      samples.fill(static_cast<int>(std::lround(price)));
    }

    // session_elapsed_fraction: best-effort, from this response's own
    // session-bounds metadata and its own last timestamp - see
    // app_core::MarketData's comment for the full reasoning. Left at the
    // IndexQuote default (1.0) if any of this is missing; never fails the
    // quote.
    float session_elapsed_fraction = 1.0f;
    long long session_start = 0;
    const cJSON* trading_period =
        cJSON_GetObjectItemCaseSensitive(meta, "currentTradingPeriod");
    const cJSON* regular_period =
        cJSON_IsObject(trading_period)
            ? cJSON_GetObjectItemCaseSensitive(trading_period, "regular")
            : nullptr;
    const cJSON* period_start =
        cJSON_IsObject(regular_period)
            ? cJSON_GetObjectItemCaseSensitive(regular_period, "start")
            : nullptr;
    const cJSON* period_end =
        cJSON_IsObject(regular_period)
            ? cJSON_GetObjectItemCaseSensitive(regular_period, "end")
            : nullptr;
    if (cJSON_IsNumber(period_start) && cJSON_IsNumber(period_end) &&
        period_end->valuedouble > period_start->valuedouble) {
      // Reported as-is, whether or not the elapsed-fraction arithmetic
      // below finds a timestamp to work with: the next-refresh decision
      // needs the session's start even from a response taken before the
      // session has produced a single bar.
      session_start = static_cast<long long>(period_start->valuedouble);
      const cJSON* timestamps =
          cJSON_GetObjectItemCaseSensitive(result0, "timestamp");
      double last_timestamp = -1.0;
      if (cJSON_IsArray(timestamps)) {
        const cJSON* stamp = nullptr;
        cJSON_ArrayForEach(stamp, timestamps) {
          if (cJSON_IsNumber(stamp)) last_timestamp = stamp->valuedouble;
        }
      }
      if (last_timestamp >= 0.0) {
        const double start = period_start->valuedouble;
        const double end = period_end->valuedouble;
        if (last_timestamp <= start || last_timestamp >= end) {
          // Not actively in this session - a completed prior session or a
          // finished current one, either way not partial. See this
          // field's own comment for why that is 1.0, not 0.0.
          session_elapsed_fraction = 1.0f;
        } else {
          session_elapsed_fraction =
              static_cast<float>((last_timestamp - start) / (end - start));
        }
      }
    }

    IndexQuote parsed;
    parsed.has_intraday = have_intraday;
    parsed.as_of_year = as_of_year;
    parsed.as_of_month = as_of_month;
    parsed.as_of_day = as_of_day;
    parsed.valid = true;
    parsed.label = display_label;
    parsed.value = static_cast<int>(std::lround(price));
    parsed.change_percent = (price - previous_close) / previous_close * 100.0;
    parsed.samples = samples;
    parsed.sample_count = sample_count;
    parsed.session_elapsed_fraction = session_elapsed_fraction;
    parsed.session_start = session_start;

    out = parsed;
    ok = true;
  } while (false);

  cJSON_Delete(root);
  return ok;
}

TaiwanFetchOutcome select_taiwan_source(bool primary_ok,
                                        const IndexQuote& primary,
                                        bool fallback_ok,
                                        const app_core::MarketData& fallback) {
  TaiwanFetchOutcome outcome;
  if (primary_ok) {
    outcome.ok = true;
    outcome.used_primary = true;
    outcome.data.display_name = "TAIWAN MARKET";
    outcome.data.valid = true;
    outcome.data.has_intraday = primary.has_intraday;
    outcome.data.as_of_year = primary.as_of_year;
    outcome.data.as_of_month = primary.as_of_month;
    outcome.data.as_of_day = primary.as_of_day;
    outcome.data.primary_label = primary.label;
    outcome.data.primary_value = primary.value;
    outcome.data.primary_change_percent = primary.change_percent;
    outcome.data.intraday_samples = primary.samples;
    outcome.data.intraday_sample_count = primary.sample_count;
    outcome.data.session_elapsed_fraction = primary.session_elapsed_fraction;
    // secondary_label left empty on purpose: see this function's own
    // comment in market_parse.hpp.
    return outcome;
  }
  if (fallback_ok) {
    outcome.ok = true;
    outcome.used_primary = false;
    outcome.data = fallback;  // parse_taiwan_index() already built this fully.
    return outcome;
  }
  outcome.ok = false;
  outcome.used_primary = false;
  outcome.data = app_core::MarketData{};
  return outcome;
}

}  // namespace market
