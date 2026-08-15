#include "market_parse.hpp"

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

HOST_TEST(market_parse_yahoo_quote_full_response_is_valid_with_downsampled_intraday) {
  market::IndexQuote quote;
  const bool ok = market::parse_yahoo_quote(
      kYahooFullResponse, std::strlen(kYahooFullResponse), "S&P 500", quote);

  EXPECT_TRUE(ok);
  EXPECT_TRUE(quote.valid);
  EXPECT_EQ(quote.label, std::string("S&P 500"));
  EXPECT_EQ(quote.value, 7786);  // lround(7785.76)
  // (7785.76 - 7798.99) / 7798.99 * 100
  EXPECT_TRUE(quote.change_percent > -0.18 && quote.change_percent < -0.16);

  // 10 raw points with one null (9 valid) downsampled to exactly 8, first
  // and last real, no interpolation - see parse_yahoo_quote's doc comment
  // for the exact index math.
  const std::array<int, 8> expected{100, 101, 102, 104, 105, 106, 107, 109};
  EXPECT_TRUE(quote.samples == expected);
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
