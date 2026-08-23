#include "test_support.hpp"

#include "app_snapshot.hpp"
#include "history.hpp"

#include <vector>

namespace {

constexpr uint32_t kIntervalMinutes = 5;

// Builds a window whose charge falls linearly from `from_mv` to `to_mv`.
std::vector<app_core::HistorySample> ramp(int from_mv, int to_mv,
                                          std::size_t count) {
  std::vector<app_core::HistorySample> samples(count);
  for (std::size_t i = 0; i < count; ++i) {
    const double fraction =
        count <= 1 ? 0.0 : static_cast<double>(i) / (count - 1);
    samples[i].battery_millivolts = static_cast<uint16_t>(
        from_mv + (to_mv - from_mv) * fraction);
  }
  return samples;
}

}  // namespace

HOST_TEST(runtime_estimate_refuses_to_guess_without_enough_history) {
  // One reading is a measurement, not a trend. Four is an ADC excursion.
  for (std::size_t count = 0; count < app_core::kMinimumSamplesForEstimate;
       ++count) {
    const auto samples = ramp(4100, 3600, count);
    const auto estimate = app_core::estimate_runtime(
        samples.data(), samples.size(), kIntervalMinutes);
    EXPECT_TRUE(!estimate.known);
    EXPECT_TRUE(estimate.trend == app_core::PowerTrend::Unknown);
  }
}

HOST_TEST(runtime_estimate_projects_a_falling_cell_to_empty) {
  // 4100 -> 3600 mV across four hours of five-minute slots.
  const auto samples = ramp(4100, 3600, 48);
  const auto estimate = app_core::estimate_runtime(
      samples.data(), samples.size(), kIntervalMinutes);

  EXPECT_TRUE(estimate.trend == app_core::PowerTrend::Discharging);
  EXPECT_TRUE(estimate.known);
  EXPECT_TRUE(estimate.percent_per_hour < 0.0f);
  // Fitted on the newest window, not on all 48 slots handed in.
  EXPECT_EQ(static_cast<int>(estimate.samples_used),
            static_cast<int>(app_core::kEstimateWindowSlots));

  // The projection has to be consistent with its own slope rather than with a
  // number pinned here: percent left divided by percent lost per hour.
  const int final_percent = app_core::battery_percent(3600);
  const double expected_minutes =
      final_percent / -static_cast<double>(estimate.percent_per_hour) * 60.0;
  const double error =
      static_cast<double>(estimate.minutes_remaining) - expected_minutes;
  EXPECT_TRUE(error < 1.0 && error > -1.0);
}

HOST_TEST(runtime_estimate_reports_charging_rather_than_a_negative_runtime) {
  // Rising voltage. Without a charge-detect line this is the only evidence
  // there is, and the wrong answer here is a runtime computed from a positive
  // slope - which projects backwards and would print a plausible number.
  const auto samples = ramp(3600, 4100, 48);
  const auto estimate = app_core::estimate_runtime(
      samples.data(), samples.size(), kIntervalMinutes);

  EXPECT_TRUE(estimate.trend == app_core::PowerTrend::Charging);
  EXPECT_TRUE(!estimate.known);
  EXPECT_EQ(static_cast<int>(estimate.minutes_remaining), 0);
}

HOST_TEST(runtime_estimate_ignores_history_older_than_its_window) {
  // Two days of discharge, then a charger. The stale slope must not outvote
  // what the cell is doing now: fitting the whole ring kept reporting
  // Discharging for hours after the cable landed, which is the bug this
  // window exists to close.
  auto samples = ramp(4100, 3600, 200);
  const auto recent = ramp(3600, 3900, app_core::kEstimateWindowSlots);
  for (std::size_t i = 0; i < recent.size(); ++i) {
    samples[samples.size() - recent.size() + i] = recent[i];
  }
  const auto estimate = app_core::estimate_runtime(
      samples.data(), samples.size(), kIntervalMinutes);

  EXPECT_TRUE(estimate.trend == app_core::PowerTrend::Charging);
  EXPECT_TRUE(estimate.percent_per_hour > 0.0f);
  EXPECT_EQ(static_cast<int>(estimate.samples_used),
            static_cast<int>(app_core::kEstimateWindowSlots));
}

HOST_TEST(runtime_estimate_projects_from_the_recent_rate_not_the_two_day_mean) {
  // Flat all day, then a real discharge. A whole-ring fit averages the two
  // into a slope that is neither, and projects a runtime from it.
  auto samples = ramp(3900, 3900, 300);
  const auto recent = ramp(3900, 3700, app_core::kEstimateWindowSlots);
  for (std::size_t i = 0; i < recent.size(); ++i) {
    samples[samples.size() - recent.size() + i] = recent[i];
  }
  const auto estimate = app_core::estimate_runtime(
      samples.data(), samples.size(), kIntervalMinutes);

  EXPECT_TRUE(estimate.trend == app_core::PowerTrend::Discharging);
  EXPECT_TRUE(estimate.known);
  // 3900 -> 3700 mV over the two-hour window is a steep, real rate; the
  // two-day mean would have been a fraction of it.
  EXPECT_TRUE(estimate.percent_per_hour < -5.0f);
}

HOST_TEST(runtime_estimate_calls_a_flat_cell_steady_rather_than_eternal) {
  // A finished charger holds the cell at a plateau. Fitting that gives a slope
  // near zero, and dividing by it is how a battery page ends up claiming
  // several months of runtime.
  std::vector<app_core::HistorySample> samples(48);
  for (auto& sample : samples) sample.battery_millivolts = 4199;
  const auto estimate = app_core::estimate_runtime(
      samples.data(), samples.size(), kIntervalMinutes);

  EXPECT_TRUE(estimate.trend == app_core::PowerTrend::Steady);
  EXPECT_TRUE(!estimate.known);
}

HOST_TEST(runtime_estimate_skips_gaps_instead_of_interpolating_them) {
  auto samples = ramp(4100, 3600, 48);
  // A boot with no battery reading leaves holes. They must not count as
  // samples and must not contribute a zero-volt point, which would drag the
  // fit into claiming a catastrophic discharge.
  for (std::size_t i = 0; i < samples.size(); i += 3) {
    samples[i].battery_millivolts = app_core::HistorySample::kNoBattery;
  }
  const auto estimate = app_core::estimate_runtime(
      samples.data(), samples.size(), kIntervalMinutes);

  EXPECT_TRUE(estimate.trend == app_core::PowerTrend::Discharging);
  EXPECT_TRUE(estimate.samples_used < 48);
  EXPECT_TRUE(estimate.samples_used > 0);
  // Still a sane rate rather than the cliff a zero-filled gap would produce.
  EXPECT_TRUE(estimate.percent_per_hour > -100.0f);
}

HOST_TEST(runtime_estimate_is_total_on_degenerate_input) {
  EXPECT_TRUE(!app_core::estimate_runtime(nullptr, 10, 5).known);
  const auto samples = ramp(4100, 3600, 48);
  // A zero interval would divide by zero on the way to hours-per-slot.
  EXPECT_TRUE(!app_core::estimate_runtime(samples.data(), samples.size(), 0)
                   .known);
}

HOST_TEST(history_blob_rejects_what_a_power_cut_leaves_behind) {
  app_core::HistoryBlob blob;
  blob.count = 3;
  blob.samples[0].battery_millivolts = 4100;
  app_core::history_blob_seal(blob, 7);
  EXPECT_TRUE(app_core::history_blob_valid(blob));

  // A write interrupted partway leaves a sector whose contents disagree with
  // its checksum. It has to be skipped, not read back as measurements.
  app_core::HistoryBlob torn = blob;
  torn.samples[0].battery_millivolts = 3000;
  EXPECT_TRUE(!app_core::history_blob_valid(torn));

  // An erased sector is all 0xFF, which is not the magic.
  app_core::HistoryBlob erased;
  erased.magic = 0xFFFFFFFFu;
  EXPECT_TRUE(!app_core::history_blob_valid(erased));

  // A count past the array would walk off the end of the samples on read.
  app_core::HistoryBlob overlong;
  overlong.count = app_core::kHistorySlots + 1;
  app_core::history_blob_seal(overlong, 8);
  EXPECT_TRUE(overlong.count == app_core::kHistorySlots);
}

HOST_TEST(history_crc32_matches_the_known_ieee_vector) {
  // "123456789" -> 0xCBF43926 is the standard CRC-32 check value. Pinning it
  // means a rewrite of the loop cannot quietly change what old sectors mean.
  const char* input = "123456789";
  EXPECT_TRUE(app_core::history_crc32(
                  reinterpret_cast<const uint8_t*>(input), 9) == 0xCBF43926u);
}

HOST_TEST(history_append_keeps_the_newest_window_oldest_first) {
  app_core::HistoryBlob blob;
  for (std::size_t i = 0; i < app_core::kHistorySlots; ++i) {
    app_core::HistorySample sample;
    sample.battery_millivolts = static_cast<uint16_t>(3000 + i);
    app_core::history_append(blob, sample);
  }
  EXPECT_EQ(static_cast<int>(blob.count),
            static_cast<int>(app_core::kHistorySlots));
  EXPECT_EQ(static_cast<int>(blob.samples[0].battery_millivolts), 3000);

  // One past full: the oldest goes, everything shifts, the newest is last.
  app_core::HistorySample extra;
  extra.battery_millivolts = 4200;
  app_core::history_append(blob, extra);
  EXPECT_EQ(static_cast<int>(blob.count),
            static_cast<int>(app_core::kHistorySlots));
  EXPECT_EQ(static_cast<int>(blob.samples[0].battery_millivolts), 3001);
  EXPECT_EQ(
      static_cast<int>(blob.samples[app_core::kHistorySlots - 1]
                           .battery_millivolts),
      4200);
}

HOST_TEST(recent_temperatures_returns_the_newest_readings_oldest_first) {
  app_core::HistoryBlob blob;
  for (int i = 0; i < 20; ++i) {
    app_core::HistorySample sample;
    // Every third slot has no sensor reading - a boot with the SHTC3 absent.
    if (i % 3 != 0) {
      sample.temperature_decic = static_cast<int16_t>(200 + i);
    }
    app_core::history_append(blob, sample);
  }

  double out[8] = {};
  const uint8_t filled = app_core::history_recent_temperatures(blob, out, 8);
  EXPECT_EQ(static_cast<int>(filled), 8);
  // Oldest-first, and strictly increasing because the source ramp was.
  for (int i = 1; i < 8; ++i) EXPECT_TRUE(out[i] > out[i - 1]);
  // The newest slot with a reading is 19 -> 21.9 C.
  EXPECT_TRUE(out[7] > 21.8 && out[7] < 22.0);

  // Fewer readings than asked for is reported, not padded with zeros - a
  // padded chart draws a line through temperatures nobody measured.
  app_core::HistoryBlob sparse;
  app_core::HistorySample one;
  one.temperature_decic = 250;
  app_core::history_append(sparse, one);
  double few[8] = {};
  EXPECT_EQ(static_cast<int>(
                app_core::history_recent_temperatures(sparse, few, 8)), 1);

  EXPECT_EQ(static_cast<int>(
                app_core::history_recent_temperatures(blob, nullptr, 8)), 0);
}

HOST_TEST(pcf85063_encoding_round_trips_and_clears_the_stop_flag) {
  const app_core::RtcDateTime original{2026, 8, 16, 23, 41, 7};
  uint8_t registers[7] = {};
  EXPECT_TRUE(app_core::encode_pcf85063(original, registers,
                                        sizeof(registers)));

  // Bit 7 of seconds is the oscillator-stop flag. If encoding ever set it, the
  // chip would store the correct time and still report itself invalid on the
  // next read - which is the exact failure this whole path exists to end.
  EXPECT_EQ(static_cast<int>(registers[0] & 0x80), 0);

  app_core::RtcDateTime decoded{};
  EXPECT_TRUE(app_core::decode_pcf85063(registers, sizeof(registers),
                                        decoded));
  EXPECT_EQ(static_cast<int>(decoded.year), 2026);
  EXPECT_EQ(static_cast<int>(decoded.month), 8);
  EXPECT_EQ(static_cast<int>(decoded.day), 16);
  EXPECT_EQ(static_cast<int>(decoded.hour), 23);
  EXPECT_EQ(static_cast<int>(decoded.minute), 41);
  EXPECT_EQ(static_cast<int>(decoded.second), 7);

  // Out of range is refused rather than written as garbage the chip would
  // then hand back as a confident-looking wrong date.
  const app_core::RtcDateTime bad_day{2026, 2, 30, 0, 0, 0};
  EXPECT_TRUE(!app_core::encode_pcf85063(bad_day, registers,
                                         sizeof(registers)));
  const app_core::RtcDateTime bad_year{1999, 1, 1, 0, 0, 0};
  EXPECT_TRUE(!app_core::encode_pcf85063(bad_year, registers,
                                         sizeof(registers)));
  EXPECT_TRUE(!app_core::encode_pcf85063(original, registers, 3));
}
