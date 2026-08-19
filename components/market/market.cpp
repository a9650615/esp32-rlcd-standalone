#include "market.hpp"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <algorithm>
#include <string>

namespace market {
namespace {

constexpr char kTag[] = "market";
constexpr int kHttpTimeoutMs = 8000;

// Bounded response buffers - an unbounded read of a remote body into a heap
// allocation is a defect even with plenty of PSRAM to spare. TWSE's
// MI_INDEX lists every index it publishes (~30-40 rows) in one response -
// 46 KB was observed live against the real endpoint while building this.
// Yahoo's 1-day/5-minute chart response for one symbol was ~7 KB live at
// 15-minute bars and stays well inside this cap at 5-minute ones too - a
// day's worth of extra timestamp/close entries is a few KB, not an order
// of magnitude.
// Both caps below are generous multiples of that, not "as much as fits": a
// response that blows the cap is simply truncated, and a truncated body
// fails to parse (see market_parse.cpp) rather than being accepted
// partially.
constexpr int kTaiwanBufferBytes = 96 * 1024;
constexpr int kYahooBufferBytes = 32 * 1024;

app_core::MarketData g_taiwan;  // valid == false until the first success.
app_core::MarketData g_us;
// See taiwan_using_primary_source() below.
bool g_taiwan_using_primary = false;
// See us_session_start() below.
long long g_us_session_start = 0;

// GETs `url`, heap-allocating up to `max_bytes` for the body (never on the
// caller's stack - MI_INDEX alone is tens of KB). Every esp_err_t and the
// HTTP status are logged; on a non-200 status the start of the body is
// logged too, so a serial capture explains a NO DATA page without
// guesswork.
bool http_get(const char* url, int max_bytes, std::string& out_body) {
  out_body.clear();

  esp_http_client_config_t config = {};
  config.url = url;
  config.timeout_ms = kHttpTimeoutMs;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    ESP_LOGE(kTag, "esp_http_client_init failed for %s", url);
    return false;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "open failed for %s: %s", url, esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  const int64_t content_length = esp_http_client_fetch_headers(client);
  if (content_length < -1) {
    ESP_LOGW(kTag, "fetch_headers failed for %s", url);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  std::string buffer(static_cast<std::size_t>(max_bytes), '\0');
  const int read = esp_http_client_read_response(client, buffer.data(), max_bytes);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (read < 0) {
    ESP_LOGW(kTag, "read failed for %s", url);
    return false;
  }
  if (status != 200) {
    ESP_LOGW(kTag, "%s returned HTTP %d: %.*s", url, status,
             std::min(read, 200), buffer.data());
    return false;
  }

  buffer.resize(static_cast<std::size_t>(read));
  out_body = std::move(buffer);
  return true;
}

}  // namespace

bool refresh_taiwan() {
  std::string primary_body;
  IndexQuote primary;
  bool primary_ok = false;
  if (http_get(
          "https://query1.finance.yahoo.com/v8/finance/chart/%5ETWII"
          "?interval=5m&range=1d",
          kYahooBufferBytes, primary_body)) {
    primary_ok =
        parse_yahoo_quote(primary_body.data(), primary_body.size(), "TAIEX",
                          primary);
    if (!primary_ok) {
      ESP_LOGW(kTag,
               "Yahoo TWII chart body did not parse (%zu bytes); falling "
               "back to TWSE",
               primary_body.size());
    }
  } else {
    ESP_LOGW(kTag, "Yahoo TWII fetch failed; falling back to TWSE");
  }

  app_core::MarketData fallback;
  bool fallback_ok = false;
  if (!primary_ok) {
    // Only reached once the primary has already failed - fetching both
    // every cycle would double the request rate for a value normally
    // discarded (see market.hpp's own comment on refresh_taiwan()).
    std::string fallback_body;
    if (http_get("https://openapi.twse.com.tw/v1/exchangeReport/MI_INDEX",
                 kTaiwanBufferBytes, fallback_body)) {
      fallback_ok = parse_taiwan_index(fallback_body.data(),
                                       fallback_body.size(), fallback);
      if (!fallback_ok) {
        ESP_LOGW(kTag, "TWSE MI_INDEX body did not parse (%zu bytes)",
                 fallback_body.size());
      }
    }
  }

  const TaiwanFetchOutcome outcome =
      select_taiwan_source(primary_ok, primary, fallback_ok, fallback);
  g_taiwan = outcome.data;
  g_taiwan_using_primary = outcome.used_primary;
  return outcome.ok;
}

bool taiwan_using_primary_source() { return g_taiwan_using_primary; }

bool refresh_us() {
  std::string sp500_body;
  if (!http_get(
          "https://query1.finance.yahoo.com/v8/finance/chart/%5EGSPC"
          "?interval=5m&range=1d",
          kYahooBufferBytes, sp500_body)) {
    g_us = app_core::MarketData{};
    return false;
  }
  IndexQuote primary;
  if (!parse_yahoo_quote(sp500_body.data(), sp500_body.size(), "S&P 500",
                          primary)) {
    ESP_LOGW(kTag, "S&P 500 chart body did not parse (%zu bytes)",
             sp500_body.size());
    g_us = app_core::MarketData{};
    return false;
  }

  std::string nasdaq_body;
  if (!http_get(
          "https://query1.finance.yahoo.com/v8/finance/chart/%5EIXIC"
          "?interval=5m&range=1d",
          kYahooBufferBytes, nasdaq_body)) {
    g_us = app_core::MarketData{};
    return false;
  }
  IndexQuote secondary;
  if (!parse_yahoo_quote(nasdaq_body.data(), nasdaq_body.size(), "NASDAQ",
                          secondary)) {
    ESP_LOGW(kTag, "NASDAQ chart body did not parse (%zu bytes)",
             nasdaq_body.size());
    g_us = app_core::MarketData{};
    return false;
  }

  app_core::MarketData parsed;
  parsed.display_name = "US MARKET";
  parsed.has_intraday = primary.has_intraday;
  // The primary index dates the page: both quotes come from the same session,
  // and taking it from one of them keeps this a reported fact rather than a
  // reconciliation of two.
  parsed.as_of_year = primary.as_of_year;
  parsed.as_of_month = primary.as_of_month;
  parsed.as_of_day = primary.as_of_day;
  parsed.primary_label = primary.label;
  parsed.primary_value = primary.value;
  parsed.primary_change_percent = primary.change_percent;
  parsed.secondary_label = secondary.label;
  parsed.secondary_value = secondary.value;
  parsed.secondary_change_percent = secondary.change_percent;
  parsed.intraday_samples = primary.samples;
  parsed.intraday_sample_count = primary.sample_count;
  // Same reasoning as the as_of date above: both quotes are the same
  // session, so the primary's own value is the reported fact, not a
  // reconciliation.
  parsed.session_elapsed_fraction = primary.session_elapsed_fraction;
  parsed.valid = true;

  g_us = parsed;
  g_us_session_start = primary.session_start;
  return true;
}

// Gated on g_us.valid rather than reset on each of refresh_us()'s four
// failure paths: one condition cannot be forgotten by a fifth one added
// later, and a scheduler must never be handed a session boundary from a
// snapshot that is no longer on screen.
long long us_session_start() { return g_us.valid ? g_us_session_start : 0; }

app_core::MarketData taiwan() { return g_taiwan; }
app_core::MarketData us() { return g_us; }

}  // namespace market
