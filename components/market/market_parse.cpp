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
    std::array<int, 8> samples{};
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
      // Bounded scratch buffer, not a heap vector: the requested range/
      // interval this component uses never produces more than a few dozen
      // points, and a hostile/garbled response is simply capped rather than
      // chased.
      constexpr std::size_t kMaxPoints = 64;
      std::array<double, kMaxPoints> valid_closes{};
      std::size_t valid_count = 0;
      const cJSON* point = nullptr;
      cJSON_ArrayForEach(point, closes) {
        if (valid_count >= kMaxPoints) break;
        if (cJSON_IsNumber(point)) valid_closes[valid_count++] = point->valuedouble;
      }
      if (valid_count >= samples.size()) {
        for (std::size_t i = 0; i < samples.size(); ++i) {
          const std::size_t idx =
              (i * (valid_count - 1)) / (samples.size() - 1);
          samples[i] = static_cast<int>(std::lround(valid_closes[idx]));
        }
        have_intraday = true;
      }
    }
    if (!have_intraday) {
      samples.fill(static_cast<int>(std::lround(price)));
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

    out = parsed;
    ok = true;
  } while (false);

  cJSON_Delete(root);
  return ok;
}

}  // namespace market
