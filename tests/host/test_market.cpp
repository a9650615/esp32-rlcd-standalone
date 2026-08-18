#include "market_parse.hpp"
#include "market_schedule.hpp"
// Header-only constants (kRefreshIntervalSeconds,
// kTaiwanFastRefreshIntervalSeconds) - market.cpp itself is not part of the
// host build, but nothing here calls its functions, so including the
// declarations is safe.
#include "market.hpp"

#include "app_snapshot.hpp"
#include "test_support.hpp"

#include <cstring>

namespace {

// A realistic (trimmed) TWSE /v1/exchangeReport/MI_INDEX response: the real
// endpoint returns one row per published index (~30-40 rows, ~46 KB
// observed live); this keeps just a handful, including the two rows the
// Taiwan page needs, with the real field names/values captured from a live
// call while building this parser.
constexpr char kTaiwanFullResponse[] = R"JSON([
  {
    "日期": "1150814",
    "指數": "寶島股價指數",
    "收盤指數": "50839.65",
    "漲跌": "-",
    "漲跌點數": "262.51",
    "漲跌百分比": "-0.51",
    "特殊處理註記": ""
  },
  {
    "日期": "1150814",
    "指數": "發行量加權股價指數",
    "收盤指數": "45811.01",
    "漲跌": "-",
    "漲跌點數": "210.47",
    "漲跌百分比": "-0.46",
    "特殊處理註記": ""
  },
  {
    "日期": "1150814",
    "指數": "臺灣公司治理100指數",
    "收盤指數": "28536.09",
    "漲跌": "-",
    "漲跌點數": "150.80",
    "漲跌百分比": "-0.53",
    "特殊處理註記": ""
  },
  {
    "日期": "1150814",
    "指數": "臺灣50指數",
    "收盤指數": "42499.44",
    "漲跌": "-",
    "漲跌點數": "285.79",
    "漲跌百分比": "-0.67",
    "特殊處理註記": ""
  }
])JSON";

// meta block matches a real ^GSPC chart response observed live; close[] is
// a synthetic 10-point series (with one null gap, as Yahoo's arrays
// routinely have) so the downsample-to-8 and null-skipping logic can be
// checked against exact expected output.
constexpr char kYahooFullResponse[] = R"JSON({
  "chart": {
    "result": [
      {
        "meta": {
          "currency": "USD",
          "symbol": "^GSPC",
          "regularMarketPrice": 7785.76,
          "previousClose": 7798.99,
          "longName": "S&P 500"
        },
        "timestamp": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
        "indicators": {
          "quote": [
            {
              "close": [100, 101, 102, null, 104, 105, 106, 107, 108, 109]
            }
          ]
        }
      }
    ],
    "error": null
  }
})JSON";

}  // namespace

HOST_TEST(market_parse_taiwan_index_full_response_is_valid) {
  app_core::MarketData data;
  const bool ok = market::parse_taiwan_index(
      kTaiwanFullResponse, std::strlen(kTaiwanFullResponse), data);

  EXPECT_TRUE(ok);
  EXPECT_TRUE(data.valid);
  EXPECT_EQ(data.primary_label, std::string("TAIEX"));
  EXPECT_EQ(data.primary_value, 45811);
  EXPECT_TRUE(data.primary_change_percent > -0.47 &&
              data.primary_change_percent < -0.45);
  EXPECT_EQ(data.secondary_label, std::string("TW50"));
  EXPECT_EQ(data.secondary_value, 42499);
  EXPECT_TRUE(data.secondary_change_percent > -0.68 &&
              data.secondary_change_percent < -0.66);

  // MI_INDEX carries no intraday series (it is a once-daily close) - every
  // sample must be the real TAIEX close, never a zero default or an
  // invented shape.
  for (const int sample : data.intraday_samples) {
    EXPECT_EQ(sample, 45811);
  }
}

HOST_TEST(market_parse_taiwan_index_truncated_body_is_invalid_and_resets) {
  const std::size_t cut = std::strlen(kTaiwanFullResponse) / 2;

  app_core::MarketData data;
  data.valid = true;  // pre-seed with a "good" value to prove it gets reset.
  data.primary_value = 99999;
  const bool ok =
      market::parse_taiwan_index(kTaiwanFullResponse, cut, data);

  EXPECT_TRUE(!ok);
  EXPECT_TRUE(!data.valid);
  EXPECT_EQ(data.primary_value, 0);
  EXPECT_EQ(data.primary_label, std::string(""));
}

HOST_TEST(market_parse_taiwan_index_missing_required_field_is_invalid) {
  // Same shape as the real row but the TAIEX entry is missing 收盤指數 -
  // syntactically valid JSON, not truncated.
  constexpr char kMissingField[] = R"JSON([
    {
      "指數": "發行量加權股價指數",
      "漲跌": "-",
      "漲跌點數": "210.47",
      "漲跌百分比": "-0.46"
    },
    {
      "指數": "臺灣50指數",
      "收盤指數": "42499.44",
      "漲跌": "-",
      "漲跌點數": "285.79",
      "漲跌百分比": "-0.67"
    }
  ])JSON";

  app_core::MarketData data;
  const bool ok = market::parse_taiwan_index(
      kMissingField, std::strlen(kMissingField), data);

  EXPECT_TRUE(!ok);
  EXPECT_TRUE(!data.valid);
}

HOST_TEST(market_parse_taiwan_index_unexpected_shape_is_invalid) {
  // TWSE's own error/maintenance response is a JSON object, not the
  // documented row array - a real "the shape changed" failure mode.
  constexpr char kUnexpectedShape[] =
      R"JSON({"error": "service temporarily unavailable"})JSON";

  app_core::MarketData data;
  const bool ok = market::parse_taiwan_index(
      kUnexpectedShape, std::strlen(kUnexpectedShape), data);

  EXPECT_TRUE(!ok);
  EXPECT_TRUE(!data.valid);
}

HOST_TEST(market_parse_taiwan_index_missing_target_rows_is_invalid) {
  // Well-formed row array, but neither TAIEX nor TW50 is present in it -
  // e.g. TWSE renames/drops the row this parser matches by name.
  constexpr char kOtherRowsOnly[] = R"JSON([
    {
      "指數": "寶島股價指數",
      "收盤指數": "50839.65",
      "漲跌": "-",
      "漲跌點數": "262.51",
      "漲跌百分比": "-0.51"
    }
  ])JSON";

  app_core::MarketData data;
  const bool ok = market::parse_taiwan_index(
      kOtherRowsOnly, std::strlen(kOtherRowsOnly), data);

  EXPECT_TRUE(!ok);
  EXPECT_TRUE(!data.valid);
}

HOST_TEST(market_parse_yahoo_quote_full_response_is_valid_with_real_intraday) {
  market::IndexQuote quote;
  const bool ok = market::parse_yahoo_quote(
      kYahooFullResponse, std::strlen(kYahooFullResponse), "S&P 500", quote);

  EXPECT_TRUE(ok);
  EXPECT_TRUE(quote.valid);
  EXPECT_EQ(quote.label, std::string("S&P 500"));
  EXPECT_EQ(quote.value, 7786);  // lround(7785.76)
  // (7785.76 - 7798.99) / 7798.99 * 100
  EXPECT_TRUE(quote.change_percent > -0.18 && quote.change_percent < -0.16);

  // 10 raw points with one null (9 valid) - fewer than
  // app_core::kIntradaySampleCount, so these are copied straight through,
  // in order, null skipped - no reduction, no interpolation.
  EXPECT_TRUE(quote.has_intraday);
  EXPECT_EQ(static_cast<int>(quote.sample_count), 9);
  const std::array<int, 9> expected{100, 101, 102, 104, 105,
                                    106, 107, 108, 109};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(quote.samples[i], expected[i]);
  }
}

HOST_TEST(market_parse_yahoo_quote_truncated_body_is_invalid_and_resets) {
  const std::size_t cut = std::strlen(kYahooFullResponse) / 2;

  market::IndexQuote quote;
  quote.valid = true;  // pre-seed with a "good" value to prove it gets reset.
  quote.value = 12345;
  const bool ok = market::parse_yahoo_quote(kYahooFullResponse, cut,
                                             "S&P 500", quote);

  EXPECT_TRUE(!ok);
  EXPECT_TRUE(!quote.valid);
  EXPECT_EQ(quote.value, 0);
  EXPECT_EQ(quote.label, std::string(""));
}

HOST_TEST(market_parse_yahoo_quote_missing_required_field_is_invalid) {
  // meta present but regularMarketPrice is missing entirely.
  constexpr char kMissingField[] = R"JSON({
    "chart": {
      "result": [
        {
          "meta": {
            "previousClose": 7798.99
          }
        }
      ],
      "error": null
    }
  })JSON";

  market::IndexQuote quote;
  const bool ok = market::parse_yahoo_quote(
      kMissingField, std::strlen(kMissingField), "S&P 500", quote);

  EXPECT_TRUE(!ok);
  EXPECT_TRUE(!quote.valid);
}

HOST_TEST(market_parse_yahoo_quote_unexpected_shape_is_invalid) {
  // Yahoo's documented failure response for a bad/delisted symbol or a
  // declined request - the realistic failure mode for this unofficial
  // endpoint: still valid JSON, but result is null and error is populated.
  constexpr char kErrorShape[] = R"JSON({
    "chart": {
      "result": null,
      "error": {
        "code": "Not Found",
        "description": "No data found, symbol may be delisted"
      }
    }
  })JSON";

  market::IndexQuote quote;
  const bool ok = market::parse_yahoo_quote(
      kErrorShape, std::strlen(kErrorShape), "S&P 500", quote);

  EXPECT_TRUE(!ok);
  EXPECT_TRUE(!quote.valid);
}

HOST_TEST(market_parse_yahoo_quote_few_intraday_points_falls_back_to_flat_line) {
  // meta is complete (so the quote itself is valid) but there is no
  // indicators/quote/close block at all - a plausible response shape for a
  // symbol with no chart data yet. Must not fail the whole quote, and must
  // not interpolate/invent a shape - every sample is the one real price.
  constexpr char kNoIntraday[] = R"JSON({
    "chart": {
      "result": [
        {
          "meta": {
            "regularMarketPrice": 26729.164,
            "previousClose": 26803.025
          }
        }
      ],
      "error": null
    }
  })JSON";

  market::IndexQuote quote;
  const bool ok = market::parse_yahoo_quote(
      kNoIntraday, std::strlen(kNoIntraday), "NASDAQ", quote);

  EXPECT_TRUE(ok);
  EXPECT_TRUE(quote.valid);
  EXPECT_EQ(quote.value, 26729);  // lround(26729.164)
  for (const int sample : quote.samples) {
    EXPECT_EQ(sample, 26729);
  }
}

HOST_TEST(market_parse_yahoo_quote_malformed_json_is_invalid) {
  constexpr char kGarbage[] = "{not json";
  market::IndexQuote quote;
  const bool ok = market::parse_yahoo_quote(kGarbage, std::strlen(kGarbage),
                                             "S&P 500", quote);
  EXPECT_TRUE(!ok);
  EXPECT_TRUE(!quote.valid);
}

// --- select_taiwan_source: pure fallback-selection logic, no I/O -----------

HOST_TEST(select_taiwan_source_primary_success_wins_and_carries_no_secondary) {
  market::IndexQuote primary;
  primary.valid = true;
  primary.label = "TAIEX";
  primary.value = 46050;
  primary.has_intraday = true;
  app_core::MarketData unused_fallback;

  const market::TaiwanFetchOutcome outcome =
      market::select_taiwan_source(true, primary, false, unused_fallback);

  EXPECT_TRUE(outcome.ok);
  EXPECT_TRUE(outcome.used_primary);
  EXPECT_TRUE(outcome.data.valid);
  EXPECT_EQ(outcome.data.primary_value, 46050);
  EXPECT_TRUE(outcome.data.has_intraday);
  // No secondary from a single-symbol source - see render_market_sidebar()
  // in components/ui/render_shared.cpp for why an empty label hides the
  // tile rather than showing a fabricated value.
  EXPECT_EQ(outcome.data.secondary_label, std::string(""));
}

HOST_TEST(select_taiwan_source_falls_back_only_when_primary_fails) {
  market::IndexQuote unused_primary;
  app_core::MarketData fallback;
  fallback.valid = true;
  fallback.primary_label = "TAIEX";
  fallback.primary_value = 45811;
  fallback.secondary_label = "TW50";
  fallback.secondary_value = 42499;

  const market::TaiwanFetchOutcome outcome =
      market::select_taiwan_source(false, unused_primary, true, fallback);

  EXPECT_TRUE(outcome.ok);
  // A fallback success must not be reported as the primary - the caller
  // (market.cpp's taiwan_using_primary_source(), and this test's sibling
  // below) relies on this to log which source actually served the data
  // and to pick the right refresh interval.
  EXPECT_TRUE(!outcome.used_primary);
  EXPECT_TRUE(outcome.data.valid);
  EXPECT_EQ(outcome.data.primary_value, 45811);
  // The fallback's own TW50 secondary survives unchanged.
  EXPECT_EQ(outcome.data.secondary_label, std::string("TW50"));
  EXPECT_EQ(outcome.data.secondary_value, 42499);
}

HOST_TEST(select_taiwan_source_both_failing_publishes_nothing) {
  market::IndexQuote unused_primary;
  app_core::MarketData unused_fallback;
  unused_fallback.valid = true;  // must not leak through when both fail.
  unused_fallback.primary_value = 99999;

  const market::TaiwanFetchOutcome outcome = market::select_taiwan_source(
      false, unused_primary, false, unused_fallback);

  EXPECT_TRUE(!outcome.ok);
  EXPECT_TRUE(!outcome.used_primary);
  EXPECT_TRUE(!outcome.data.valid);
  EXPECT_EQ(outcome.data.primary_value, 0);
}

// --- market_schedule.hpp: pure "when to refresh Taiwan" policy -------------

namespace {
app_core::RtcDateTime local(uint16_t year, uint8_t month, uint8_t day,
                            uint8_t hour, uint8_t minute) {
  app_core::RtcDateTime date{};
  date.year = year;
  date.month = month;
  date.day = day;
  date.hour = hour;
  date.minute = minute;
  return date;
}
}  // namespace

HOST_TEST(taiwan_market_hours_is_true_on_a_weekday_inside_the_session) {
  // 2026-08-17 is a Monday.
  EXPECT_TRUE(market::taiwan_market_hours(local(2026, 8, 17, 9, 0)));
  EXPECT_TRUE(market::taiwan_market_hours(local(2026, 8, 17, 11, 30)));
  EXPECT_TRUE(market::taiwan_market_hours(local(2026, 8, 17, 13, 30)));
}

HOST_TEST(taiwan_market_hours_is_false_on_a_weekday_outside_the_session) {
  EXPECT_TRUE(!market::taiwan_market_hours(local(2026, 8, 17, 8, 59)));
  EXPECT_TRUE(!market::taiwan_market_hours(local(2026, 8, 17, 13, 31)));
  EXPECT_TRUE(!market::taiwan_market_hours(local(2026, 8, 17, 20, 0)));
  EXPECT_TRUE(!market::taiwan_market_hours(local(2026, 8, 17, 0, 0)));
}

HOST_TEST(taiwan_market_hours_is_false_on_the_weekend_at_the_same_clock_time) {
  // 2026-08-15 is a Saturday, 2026-08-16 a Sunday - same 10:00 that reads
  // as open on the Monday right after them.
  EXPECT_TRUE(!market::taiwan_market_hours(local(2026, 8, 15, 10, 0)));
  EXPECT_TRUE(!market::taiwan_market_hours(local(2026, 8, 16, 10, 0)));
}

HOST_TEST(taiwan_refresh_interval_is_fast_only_during_hours_on_the_primary) {
  const auto in_hours = local(2026, 8, 17, 10, 0);
  const auto out_of_hours = local(2026, 8, 17, 20, 0);
  const auto weekend = local(2026, 8, 15, 10, 0);

  EXPECT_EQ(market::taiwan_refresh_interval_seconds(in_hours, true),
            market::kTaiwanFastRefreshIntervalSeconds);
  EXPECT_EQ(market::taiwan_refresh_interval_seconds(out_of_hours, true),
            market::kRefreshIntervalSeconds);
  EXPECT_EQ(market::taiwan_refresh_interval_seconds(weekend, true),
            market::kRefreshIntervalSeconds);

  // The fallback (TWSE) cannot change until after the close - refreshing it
  // quickly during market hours would just re-fetch an unchanged number, so
  // it always gets the slow interval regardless of the clock.
  EXPECT_EQ(market::taiwan_refresh_interval_seconds(in_hours, false),
            market::kRefreshIntervalSeconds);
}

// --- market_schedule.hpp: pure "when to refresh US" policy -----------------
//
// Real numbers from the response that motivated this: 2026-08-18's US
// regular session, start 1787059800, end 1787083200 (epoch seconds, as the
// source itself reported them - no timezone or DST anywhere in this test
// for the same reason there is none in the code).

HOST_TEST(us_refresh_lands_just_after_an_open_the_flat_interval_would_skip) {
  constexpr long long kOpen = 1'787'059'800;

  // Ten minutes before the open. The flat interval would next look 30
  // minutes from now - 20 minutes into a session it would still be
  // rendering as yesterday's. Sleep to the open plus the warm-up instead.
  EXPECT_EQ(market::us_refresh_interval_seconds(kOpen - 600, kOpen),
            600 + market::kUsOpenWarmupSeconds);

  // One second before the open: same rule, not a special case.
  EXPECT_EQ(market::us_refresh_interval_seconds(kOpen - 1, kOpen),
            1 + market::kUsOpenWarmupSeconds);
}

HOST_TEST(us_refresh_inside_the_warmup_window_is_floored_not_a_hot_loop) {
  constexpr long long kOpen = 1'787'059'800;
  // At the open itself, the warm-up is the whole sleep.
  EXPECT_EQ(market::us_refresh_interval_seconds(kOpen, kOpen),
            market::kUsOpenWarmupSeconds);
  // A second before the warm-up ends the remaining wait is 1 second. Two
  // HTTPS round trips a second apart is a battery drain and a way to get
  // rate-limited, so the floor takes over - the only place it can.
  EXPECT_EQ(market::us_refresh_interval_seconds(
                kOpen + market::kUsOpenWarmupSeconds - 1, kOpen),
            60);
}

HOST_TEST(us_refresh_is_the_flat_interval_whenever_there_is_no_open_to_meet) {
  constexpr long long kOpen = 1'787'059'800;
  constexpr long long kClose = 1'787'083'200;

  // Mid-session: this page is not polled faster during trading hours, only
  // phased to start on time. The request budget is unchanged.
  EXPECT_EQ(market::us_refresh_interval_seconds(kOpen + 3600, kOpen),
            market::kRefreshIntervalSeconds);
  // After the close, still reading the same session's start.
  EXPECT_EQ(market::us_refresh_interval_seconds(kClose + 3600, kOpen),
            market::kRefreshIntervalSeconds);
  // Overnight, with the source already reporting tomorrow's open: too far
  // out to align to in one sleep.
  EXPECT_EQ(market::us_refresh_interval_seconds(kOpen - 12 * 3600, kOpen),
            market::kRefreshIntervalSeconds);
  // Exactly one interval out, the boundary itself: still the flat value,
  // and the sleep after it is the one that lands short.
  EXPECT_EQ(
      market::us_refresh_interval_seconds(
          kOpen + market::kUsOpenWarmupSeconds - market::kRefreshIntervalSeconds,
          kOpen),
      market::kRefreshIntervalSeconds);
}

HOST_TEST(us_refresh_falls_back_to_the_flat_interval_without_a_clock) {
  constexpr long long kOpen = 1'787'059'800;
  // No synced clock (the caller passes 0 rather than guessing an instant),
  // and a response that carried no session bounds. Neither may shorten a
  // sleep on arithmetic over a number that means nothing.
  EXPECT_EQ(market::us_refresh_interval_seconds(0, kOpen),
            market::kRefreshIntervalSeconds);
  EXPECT_EQ(market::us_refresh_interval_seconds(kOpen - 600, 0),
            market::kRefreshIntervalSeconds);
  EXPECT_EQ(market::us_refresh_interval_seconds(0, 0),
            market::kRefreshIntervalSeconds);
}

// --- reduce_to_extremes: the whole point of this item ----------------------

HOST_TEST(reduce_to_extremes_preserves_a_single_sharp_spike) {
  // Double the output resolution (128 raw points into 64 slots, so most
  // buckets hold exactly 2 raw points) - flat at 100 except one sharp
  // spike to 500. A stride sample (every other raw point) has a coin-flip
  // chance of landing on the spike's own index or its flat neighbour -
  // exactly the defect this function exists to fix.
  std::array<double, 128> raw{};
  raw.fill(100.0);
  raw[77] = 500.0;

  std::array<int, app_core::kIntradaySampleCount> out{};
  market::reduce_to_extremes(raw.data(), raw.size(), out);

  bool spike_survived = false;
  for (const int value : out) {
    if (value == 500) spike_survived = true;
    // Every other bucket is flat, so nothing but the spike's own bucket
    // should ever read as anything other than the flat 100 baseline.
    EXPECT_TRUE(value == 100 || value == 500);
  }
  EXPECT_TRUE(spike_survived);
}

HOST_TEST(reduce_to_extremes_preserves_a_sharp_dip_too) {
  std::array<double, 128> raw{};
  raw.fill(7800.0);
  raw[40] = 7600.0;  // a dip, not a spike - the deviation rule must not
                     // only favour values above the mean.

  std::array<int, app_core::kIntradaySampleCount> out{};
  market::reduce_to_extremes(raw.data(), raw.size(), out);

  bool dip_survived = false;
  for (const int value : out) {
    if (value == 7600) dip_survived = true;
  }
  EXPECT_TRUE(dip_survived);
}

HOST_TEST(reduce_to_extremes_is_exact_when_bucket_size_is_one) {
  // raw_count == out.size(): every bucket holds exactly one point, so the
  // reduction must be a lossless, in-order copy.
  std::array<double, app_core::kIntradaySampleCount> raw{};
  for (std::size_t i = 0; i < raw.size(); ++i) {
    raw[i] = static_cast<double>(i * 7 % 97);  // an arbitrary, non-monotonic
                                               // sequence - nothing here
                                               // should get reordered.
  }
  std::array<int, app_core::kIntradaySampleCount> out{};
  market::reduce_to_extremes(raw.data(), raw.size(), out);
  for (std::size_t i = 0; i < raw.size(); ++i) {
    EXPECT_EQ(out[i], static_cast<int>(raw[i]));
  }
}

// --- MarketData::session_elapsed_fraction, computed in parse_yahoo_quote --

namespace {
// A minimal Yahoo chart response with the fields parse_yahoo_quote actually
// reads: meta.regularMarketPrice/previousClose (required), an intraday
// close/timestamp series of `bar_count` 5-minute bars starting at
// `session_start`, and meta.currentTradingPeriod.regular.start/end.
std::string yahoo_response_with_session(long long session_start,
                                        long long session_end,
                                        int bar_count) {
  std::string closes = "[";
  std::string timestamps = "[";
  for (int i = 0; i < bar_count; ++i) {
    if (i != 0) {
      closes += ",";
      timestamps += ",";
    }
    closes += std::to_string(100 + i);
    timestamps += std::to_string(session_start + i * 300);
  }
  closes += "]";
  timestamps += "]";

  // Plain concatenation, not snprintf into a fixed buffer: bar_count varies
  // per test (including a 79-bar case), and a fixed-size buffer would
  // silently truncate rather than fail loudly if a caller ever asked for
  // more bars than it was sized for.
  return "{\"chart\":{\"result\":[{\"meta\":{"
        "\"regularMarketPrice\":200.0,\"previousClose\":190.0,"
        "\"currentTradingPeriod\":{\"regular\":{\"start\":" +
        std::to_string(session_start) +
        ",\"end\":" + std::to_string(session_end) +
        "}}},\"timestamp\":" + timestamps +
        ",\"indicators\":{\"quote\":[{\"close\":" + closes +
        "}]}}],\"error\":null}}";
}
}  // namespace

HOST_TEST(session_elapsed_fraction_is_partial_mid_session) {
  // A 270-minute (Taiwan-length) session, 30 5-minute bars in (150 of the
  // 270 minutes) - just over half.
  constexpr long long kStart = 1'700'000'000;
  constexpr long long kEnd = kStart + 270 * 60;
  const std::string body =
      yahoo_response_with_session(kStart, kEnd, /*bar_count=*/30);

  market::IndexQuote quote;
  const bool ok =
      market::parse_yahoo_quote(body.data(), body.size(), "TEST", quote);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(quote.valid);
  // Last bar at kStart + 29*300 = kStart + 8700s into a 16200s session.
  const double expected = 8700.0 / 16200.0;
  EXPECT_TRUE(quote.session_elapsed_fraction > expected - 0.01 &&
              quote.session_elapsed_fraction < expected + 0.01);
}

HOST_TEST(session_elapsed_fraction_is_one_when_the_session_is_complete) {
  constexpr long long kStart = 1'700'000'000;
  constexpr long long kEnd = kStart + 270 * 60;
  // Last bar timestamp equal to the session end - a genuinely finished
  // session, live-verified as the real shape Yahoo returns after the
  // close (last timestamp == currentTradingPeriod.regular.end exactly).
  const int bar_count = static_cast<int>((kEnd - kStart) / 300) + 1;
  const std::string body =
      yahoo_response_with_session(kStart, kEnd, bar_count);

  market::IndexQuote quote;
  const bool ok =
      market::parse_yahoo_quote(body.data(), body.size(), "TEST", quote);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(quote.session_elapsed_fraction == 1.0f);
}

HOST_TEST(session_elapsed_fraction_defaults_to_one_without_trading_period) {
  // meta present and sufficient (price/previousClose), but no
  // currentTradingPeriod block at all - a plausible response shape this
  // must not fail over.
  constexpr char kNoTradingPeriod[] = R"JSON({
    "chart": {
      "result": [{
        "meta": {
          "regularMarketPrice": 200.0,
          "previousClose": 190.0
        }
      }],
      "error": null
    }
  })JSON";
  market::IndexQuote quote;
  const bool ok = market::parse_yahoo_quote(
      kNoTradingPeriod, std::strlen(kNoTradingPeriod), "TEST", quote);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(quote.session_elapsed_fraction == 1.0f);
}

// --- has_intraday / sample_count: below, at, and above the target resolution

HOST_TEST(fewer_raw_points_than_the_target_copies_through_without_padding) {
  constexpr long long kStart = 1'700'000'000;
  // kMinIntradayPoints (2) exactly - the smallest count that still counts
  // as real, i.e. the first minutes of a live session, well under
  // kIntradaySampleCount (64), so no reduction should happen at all.
  const std::string body =
      yahoo_response_with_session(kStart, kStart + 270 * 60, /*bar_count=*/2);

  market::IndexQuote quote;
  const bool ok =
      market::parse_yahoo_quote(body.data(), body.size(), "TEST", quote);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(quote.has_intraday);
  EXPECT_EQ(static_cast<int>(quote.sample_count), 2);
  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(quote.samples[i], 100 + i);
  }
}

HOST_TEST(one_fewer_raw_point_than_the_minimum_has_no_intraday_series) {
  constexpr long long kStart = 1'700'000'000;
  const std::string body =
      yahoo_response_with_session(kStart, kStart + 270 * 60, /*bar_count=*/1);

  market::IndexQuote quote;
  const bool ok =
      market::parse_yahoo_quote(body.data(), body.size(), "TEST", quote);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(!quote.has_intraday);
}

HOST_TEST(more_raw_points_than_the_target_reduce_to_exactly_the_target) {
  constexpr long long kStart = 1'700'000'000;
  const std::string body = yahoo_response_with_session(
      kStart, kStart + 270 * 60, /*bar_count=*/79);  // a real US 5m day.

  market::IndexQuote quote;
  const bool ok =
      market::parse_yahoo_quote(body.data(), body.size(), "TEST", quote);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(quote.has_intraday);
  EXPECT_EQ(static_cast<int>(quote.sample_count),
            static_cast<int>(app_core::kIntradaySampleCount));
}

HOST_TEST(session_start_is_reported_even_before_the_first_bar_exists) {
  // The scheduler needs the open from a response taken before the session
  // has produced anything to chart - the case that decides when to look
  // again.
  constexpr long long kStart = 1'700'000'000;
  const std::string body =
      yahoo_response_with_session(kStart, kStart + 270 * 60, /*bar_count=*/0);

  market::IndexQuote quote;
  const bool ok =
      market::parse_yahoo_quote(body.data(), body.size(), "TEST", quote);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(!quote.has_intraday);
  EXPECT_TRUE(quote.session_start == kStart);
}
