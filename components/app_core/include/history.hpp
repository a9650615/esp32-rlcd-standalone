#pragma once

#include <cstddef>
#include <cstdint>

namespace app_core {

// One slot of the fixed-interval history both the battery and the sensor write
// into. Fixed interval rather than a timestamp per record: the interval is a
// constant of the recorder, so storing it 65 000 times would spend a third of
// the record on something already known.
//
// Each field carries its own "not recorded" value because the three sources
// fail independently - the SHTC3 can be absent on a boot where the battery
// divider reads fine, and a slot half-filled must not average in a zero that
// nobody measured. This is the same rule the charts already follow with
// temperature_history_count.
struct HistorySample {
  static constexpr uint16_t kNoBattery = 0;
  static constexpr int16_t kNoTemperature = INT16_MIN;
  static constexpr uint8_t kNoHumidity = 0xFF;

  uint16_t battery_millivolts = kNoBattery;
  // Tenths of a degree. int16 covers -3276.8..3276.7 C, which is every
  // temperature this sensor can survive, in half the space of a float.
  int16_t temperature_decic = kNoTemperature;
  uint8_t humidity_percent = kNoHumidity;

  constexpr bool has_battery() const {
    return battery_millivolts != kNoBattery;
  }
  constexpr bool has_temperature() const {
    return temperature_decic != kNoTemperature;
  }
  constexpr bool has_humidity() const {
    return humidity_percent != kNoHumidity;
  }
};

// What the voltage trend says the board is doing.
//
// There is no charge-detect line to read: the battery connection is a
// read-only sense divider with no charger-enable, so this is inferred from the
// direction the voltage is moving and nothing else.
enum class PowerTrend : uint8_t {
  // Not enough history yet. Distinct from Steady, which is a measurement.
  Unknown,
  Charging,
  Discharging,
  // Moving too slowly to call, which is also every reading taken while the
  // cell sits on a finished charger.
  Steady,
};

struct RuntimeEstimate {
  PowerTrend trend = PowerTrend::Unknown;
  // False whenever `minutes_remaining` would be a guess: while charging, while
  // steady, and before there is enough history. The UI must show nothing
  // rather than a number in those cases - a fabricated runtime is worse than
  // an absent one, because it will be believed.
  bool known = false;
  uint32_t minutes_remaining = 0;
  // Percent per hour, negative while discharging. Reported even when `known`
  // is false so a caller can show the rate without the projection.
  float percent_per_hour = 0.0f;
  // How many slots carried a battery reading. A projection from four samples
  // and one from four hundred are not the same claim.
  uint16_t samples_used = 0;
};

// Least-squares slope of charge against time, projected to empty.
//
// Fitted on percent rather than millivolts because a lithium cell's voltage is
// nowhere near linear in charge - the plateau between roughly 3.7 V and 3.9 V
// covers over half the usable capacity, so a slope in millivolts per hour
// would call that stretch "barely discharging" and then collapse at the knee.
// battery_percent() already carries the discharge curve; fitting downstream of
// it makes the straight line a reasonable model.
//
// `samples` is oldest-first. Slots with no battery reading are skipped rather
// than interpolated, so a gap costs precision and never invents a measurement.
//
// `interval_minutes` is the spacing between adjacent slots.
RuntimeEstimate estimate_runtime(const HistorySample* samples,
                                 std::size_t count,
                                 uint32_t interval_minutes);

// Below this the fit is dominated by ADC noise rather than by discharge, and
// the honest answer is Steady. A cell that genuinely empties in a day moves at
// about 4%/h, so this rejects roughly the bottom tenth of real rates - which
// is the right trade, since the alternative is projecting "9 days remaining"
// from noise.
inline constexpr float kTrendThresholdPercentPerHour = 0.4f;

// One hour of slots at the recording interval. Fewer than this and a fit is
// reading the noise: a single ADC excursion across four samples produces a
// confident slope pointing wherever the excursion went.
inline constexpr uint16_t kMinimumSamplesForEstimate = 12;

// --- what gets written to flash -------------------------------------------
//
// 48 hours at five-minute resolution. Long enough that a runtime projection
// has a real discharge to fit rather than an hour of noise, short enough that
// the whole thing is one flash sector and can be written as a single unit.
inline constexpr std::size_t kHistorySlots = 576;
inline constexpr uint32_t kHistoryIntervalMinutes = 5;
inline constexpr uint32_t kHistoryMagic = 0x524C4348;  // "RLCH"

// The entire ring, written whole into one 4 KiB sector rather than appended to
// as individual records.
//
// A record-level append log would lose less on a power cut and costs about
// twice the code; the deciding factor is that whole-blob rotation across every
// sector of the partition already puts the wear ceiling centuries out, so the
// extra machinery buys nothing but a smaller worst-case data loss - and the
// worst case here is five minutes of a temperature chart.
//
// `seq` increases with every write and is what identifies the newest copy on
// boot. `crc` covers everything after it, so a write interrupted by a power
// cut leaves a sector that fails its own checksum and is skipped rather than
// read back as plausible-looking measurements.
struct HistoryBlob {
  uint32_t magic = kHistoryMagic;
  uint32_t seq = 0;
  uint32_t crc = 0;
  // Slots filled so far, oldest first, saturating at kHistorySlots once the
  // ring has wrapped. Same reason temperature_history_count exists: without
  // it, unwritten leading slots are indistinguishable from real readings.
  uint16_t count = 0;
  uint16_t reserved = 0;
  HistorySample samples[kHistorySlots];
};

// One sector, with room left over. Static rather than a runtime check because
// growing kHistorySlots past this is a silent corruption, not an error: the
// write would be truncated at the sector boundary and the tail would read back
// as whatever the next sector holds.
static_assert(sizeof(HistoryBlob) <= 4096,
              "HistoryBlob must fit in a single flash sector");

// Plain CRC-32 (IEEE polynomial, reflected). Written here rather than taken
// from esp_crc32_le so the host tests validate the same function the device
// runs - a checksum that only exists on one side of the build is a checksum
// nobody has tested.
uint32_t history_crc32(const uint8_t* data, std::size_t length);

// Recomputes the checksum and checks the magic. Everything read from flash
// goes through this before any of it is believed.
bool history_blob_valid(const HistoryBlob& blob);

// Sets magic, count clamp and crc so the blob is ready to write.
void history_blob_seal(HistoryBlob& blob, uint32_t seq);

// Appends one slot, dropping the oldest once the ring is full.
//
// Shifts the array rather than carrying a head index. That is a 3.5 KiB
// memmove once every five minutes, which is nothing, and it buys the property
// every reader of this structure wants: samples are always oldest-first with
// no wrap point to reason about. A head index would push that reasoning into
// the chart renderer and the estimator, which is where an off-by-one becomes a
// line drawn through the wrong day.
void history_append(HistoryBlob& blob, const HistorySample& sample);

// Fills `out` with the newest temperature readings, oldest-first, and returns
// how many were written.
//
// This is what makes the sensor chart survive a reboot: it draws eight points,
// the store keeps 576, and the eight it wants are the most recent eight that
// actually hold a reading. Slots recorded on a boot where the SHTC3 was absent
// are skipped rather than counted, for the same reason the estimator skips
// them - a gap must cost resolution, never invent a measurement.
uint8_t history_recent_temperatures(const HistoryBlob& blob, double* out,
                                    uint8_t out_count);

}  // namespace app_core
