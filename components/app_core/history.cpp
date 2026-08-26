#include "history.hpp"

#include "app_snapshot.hpp"

#include <cmath>
#include <cstddef>

namespace app_core {

RuntimeEstimate estimate_runtime(const HistorySample* samples,
                                 std::size_t count,
                                 uint32_t interval_minutes) {
  RuntimeEstimate estimate;
  if (samples == nullptr || count == 0 || interval_minutes == 0) {
    return estimate;
  }

  // Where the run the newest reading belongs to began.
  //
  // Fitting a line across a charge and a discharge together fits two
  // different things at once, so the window has to stop at the boundary. Found
  // by walking back from the newest reading for as long as the readings stay
  // consistent with one direction: through a discharge, older readings are
  // higher; through a charge, older readings are lower.
  //
  // Both directions are tried and the longer run wins, rather than guessing
  // the direction first from the last few samples. Guessing needs a
  // tie-break for a flat cell and gets it wrong exactly when the readings are
  // quietest; running the same loop twice costs nothing and has no such case.
  // Both matter: a board that booted onto a charger has nothing but the rising
  // run, and it is the only thing that can tell the panel so for the first
  // eleven minutes (see ui::battery_is_charging).
  //
  // The margin is what keeps quantisation from ending a run early: percent
  // has one-point resolution and the ADC noise under it is worth a point or
  // two, so a cell sitting still produces a sawtooth that is not a direction
  // change.
  //
  // Slots with no reading extend the run rather than ending it. A boot where
  // the divider was unreadable is a hole in the evidence, not evidence of a
  // charger.
  const auto run_start = [&](bool rising) {
    std::size_t begin = count;
    int extreme = -1;
    for (std::size_t i = count; i > 0; --i) {
      const HistorySample& sample = samples[i - 1];
      if (sample.has_battery()) {
        const int percent =
            battery_percent(static_cast<int>(sample.battery_millivolts));
        if (extreme >= 0) {
          const bool broke = rising
                                 ? percent > extreme + kDirectionChangeMarginPercent
                                 : percent < extreme - kDirectionChangeMarginPercent;
          if (broke) break;
        }
        if (extreme < 0 || (rising ? percent < extreme : percent > extreme)) {
          extreme = percent;
        }
      }
      begin = i - 1;
    }
    return begin;
  };
  const std::size_t falling_begin = run_start(false);
  const std::size_t rising_begin = run_start(true);
  const std::size_t begin =
      falling_begin < rising_begin ? falling_begin : rising_begin;

  // Accumulated in double rather than float: over a week of five-minute slots
  // the sum of t squared reaches the low millions, where float has already
  // lost the resolution that separates one slot from the next.
  double sum_t = 0.0;
  double sum_p = 0.0;
  double sum_tp = 0.0;
  double sum_tt = 0.0;
  double sum_pp = 0.0;
  uint16_t used = 0;
  double last_percent = 0.0;

  const double hours_per_slot = static_cast<double>(interval_minutes) / 60.0;
  for (std::size_t index = begin; index < count; ++index) {
    if (!samples[index].has_battery()) continue;
    // Time measured from the start of the window; only the slope is used, so
    // the origin does not matter, but keeping it small keeps the sums small.
    const double t = static_cast<double>(index - begin) * hours_per_slot;
    const double p = static_cast<double>(
        battery_percent(static_cast<int>(samples[index].battery_millivolts)));
    sum_t += t;
    sum_p += p;
    sum_tp += t * p;
    sum_tt += t * t;
    sum_pp += p * p;
    last_percent = p;
    ++used;
  }

  estimate.samples_used = used;
  if (used < kMinimumSamplesForEstimate) return estimate;

  const double n = static_cast<double>(used);
  // Centred sums. Written this way rather than as the raw-moment formulas
  // because the residual sum of squares below needs all three, and computing
  // them once keeps the slope and its uncertainty provably consistent.
  const double s_tt = sum_tt - sum_t * sum_t / n;
  const double s_tp = sum_tp - sum_t * sum_p / n;
  const double s_pp = sum_pp - sum_p * sum_p / n;
  // Zero when every retained sample landed in the same slot, which cannot
  // happen through the recorder but can through a hand-built array. Guarding
  // it here rather than trusting the caller keeps this function total.
  if (s_tt <= 0.0) return estimate;

  const double slope = s_tp / s_tt;
  estimate.percent_per_hour = static_cast<float>(slope);

  // How well this slope is actually pinned down, which is the whole reason a
  // fixed threshold was the wrong instrument.
  //
  // kTrendThresholdPercentPerHour used to be 0.4 %/h, chosen when the board
  // drained at 2.9 %/h. Power work then took it to 0.30 %/h - underneath the
  // threshold's own floor - so the estimator reported Steady forever and the
  // panel showed no runtime at all. A constant sized against one era's noise
  // cannot follow the hardware.
  //
  // The standard error of a least-squares slope does follow it: it shrinks
  // with the square root of the sample count and with the spread of the time
  // axis, so an hour of history resolves only a fast drain while two days
  // resolves a slow one. Comparing the slope against a multiple of its own
  // error asks the only question worth asking - "is this distinguishable from
  // flat, given how much was actually measured" - and gets stricter or looser
  // on its own as the evidence does.
  const double residual_sum_squares = s_pp - slope * s_tp;
  const double stderr_slope =
      residual_sum_squares > 0.0
          ? std::sqrt(residual_sum_squares / (n - 2.0) / s_tt)
          : 0.0;
  estimate.percent_per_hour_stderr = static_cast<float>(stderr_slope);

  // A perfect fit has no residual and therefore no error, which makes any
  // non-zero slope significant - correct, and only reachable from synthetic
  // data, since real readings always carry noise.
  const double magnitude = slope < 0.0 ? -slope : slope;
  if (magnitude <= kSlopeSignificanceSigmas * stderr_slope) {
    estimate.trend = PowerTrend::Steady;
    return estimate;
  }

  if (slope > 0.0) {
    // Rising charge with no charge-detect line to confirm it. Nothing else
    // makes a cell gain voltage, so this is as certain as this hardware gets.
    estimate.trend = PowerTrend::Charging;
    return estimate;
  }

  estimate.trend = PowerTrend::Discharging;
  const double minutes = last_percent / -slope * 60.0;
  // Clamped rather than left to overflow the cast. A near-threshold slope
  // against a full cell projects into the hundreds of hours, which is not a
  // useful number but is a legal one; anything past a month is reported as a
  // month rather than as a wrapped integer.
  constexpr double kMaxMinutes = 30.0 * 24.0 * 60.0;
  estimate.minutes_remaining =
      static_cast<uint32_t>(minutes > kMaxMinutes ? kMaxMinutes : minutes);
  estimate.known = true;
  return estimate;
}

namespace {

// Offset of the first byte the checksum covers: everything after `crc`.
constexpr std::size_t kCrcSkip = offsetof(HistoryBlob, count);

}  // namespace

uint32_t history_crc32(const uint8_t* data, std::size_t length) {
  uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      // Branch on the low bit rather than table lookup: this runs once every
      // five minutes over three kilobytes, so a 1 KiB table would cost more
      // than it saves.
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }
  }
  return ~crc;
}

bool history_blob_valid(const HistoryBlob& blob) {
  if (blob.magic != kHistoryMagic) return false;
  if (blob.count > kHistorySlots) return false;
  const auto* bytes = reinterpret_cast<const uint8_t*>(&blob);
  return blob.crc ==
         history_crc32(bytes + kCrcSkip, sizeof(HistoryBlob) - kCrcSkip);
}

void history_blob_seal(HistoryBlob& blob, uint32_t seq) {
  blob.magic = kHistoryMagic;
  blob.seq = seq;
  if (blob.count > kHistorySlots) blob.count = kHistorySlots;
  blob.reserved = 0;
  const auto* bytes = reinterpret_cast<const uint8_t*>(&blob);
  blob.crc = history_crc32(bytes + kCrcSkip, sizeof(HistoryBlob) - kCrcSkip);
}

void history_append(HistoryBlob& blob, const HistorySample& sample) {
  if (blob.count < kHistorySlots) {
    blob.samples[blob.count] = sample;
    ++blob.count;
    return;
  }
  // Full: drop the oldest and land the new one at the end.
  for (std::size_t i = 1; i < kHistorySlots; ++i) {
    blob.samples[i - 1] = blob.samples[i];
  }
  blob.samples[kHistorySlots - 1] = sample;
}

uint8_t history_recent_temperatures(const HistoryBlob& blob, double* out,
                                    uint8_t out_count) {
  if (out == nullptr || out_count == 0) return 0;
  // Walk backwards collecting the newest first, then reverse: the caller wants
  // oldest-first, and searching forwards would mean finding the start before
  // knowing how many there are.
  uint8_t found = 0;
  const std::size_t limit =
      blob.count < kHistorySlots ? blob.count : kHistorySlots;
  for (std::size_t i = limit; i > 0 && found < out_count; --i) {
    const HistorySample& sample = blob.samples[i - 1];
    if (!sample.has_temperature()) continue;
    out[found] = static_cast<double>(sample.temperature_decic) / 10.0;
    ++found;
  }
  for (uint8_t i = 0; i < found / 2; ++i) {
    const double swap = out[i];
    out[i] = out[found - 1 - i];
    out[found - 1 - i] = swap;
  }
  return found;
}

}  // namespace app_core
