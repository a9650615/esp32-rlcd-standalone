#include "history.hpp"

#include "app_snapshot.hpp"

#include <cstddef>

namespace app_core {

RuntimeEstimate estimate_runtime(const HistorySample* samples,
                                 std::size_t count,
                                 uint32_t interval_minutes) {
  RuntimeEstimate estimate;
  if (samples == nullptr || count == 0 || interval_minutes == 0) {
    return estimate;
  }

  // Accumulated in double rather than float: over a week of five-minute slots
  // the sum of t squared reaches the low millions, where float has already
  // lost the resolution that separates one slot from the next.
  double sum_t = 0.0;
  double sum_p = 0.0;
  double sum_tp = 0.0;
  double sum_tt = 0.0;
  uint16_t used = 0;
  double last_percent = 0.0;

  const double hours_per_slot = static_cast<double>(interval_minutes) / 60.0;
  for (std::size_t index = 0; index < count; ++index) {
    if (!samples[index].has_battery()) continue;
    // Time measured from the start of the window; only the slope is used, so
    // the origin does not matter, but keeping it small keeps the sums small.
    const double t = static_cast<double>(index) * hours_per_slot;
    const double p = static_cast<double>(
        battery_percent(static_cast<int>(samples[index].battery_millivolts)));
    sum_t += t;
    sum_p += p;
    sum_tp += t * p;
    sum_tt += t * t;
    last_percent = p;
    ++used;
  }

  estimate.samples_used = used;
  if (used < kMinimumSamplesForEstimate) return estimate;

  const double n = static_cast<double>(used);
  const double denominator = n * sum_tt - sum_t * sum_t;
  // Zero when every retained sample landed in the same slot, which cannot
  // happen through the recorder but can through a hand-built array. Guarding
  // it here rather than trusting the caller keeps this function total.
  if (denominator == 0.0) return estimate;

  const double slope = (n * sum_tp - sum_t * sum_p) / denominator;
  estimate.percent_per_hour = static_cast<float>(slope);

  if (slope > kTrendThresholdPercentPerHour) {
    // Rising charge with no charge-detect line to confirm it. Nothing else
    // makes a cell gain voltage, so this is as certain as this hardware gets.
    estimate.trend = PowerTrend::Charging;
    return estimate;
  }
  if (slope > -kTrendThresholdPercentPerHour) {
    estimate.trend = PowerTrend::Steady;
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
