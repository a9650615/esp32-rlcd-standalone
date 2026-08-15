#pragma once

#include <cstddef>
#include <cstdint>

namespace board {

// SHTC3 address, command words, and CRC-8 definition sourced from Waveshare's
// own factory ESP-IDF example (05_I2C_SHTC3/components/port_bsp/
// i2c_equipment.h, commit eb1f63427d735a22b9c30e22fa63ebddae1834d3, pinned by
// .agents/skills/esp32-s3-rlcd-dev/references/official-development.md), which
// matches the public SHTC3 datasheet command table. ESP-IDF itself (checked
// under .tools/esp-idf/) ships no SHTC3 driver - it is not a first-party IDF
// peripheral.
inline constexpr uint8_t kShtc3Address = 0x70;
inline constexpr uint16_t kShtc3CmdWakeup = 0x3517;
// Read T first, clock stretching disabled - the polling-friendly variant.
inline constexpr uint16_t kShtc3CmdMeasureTFirst = 0x7866;
inline constexpr uint16_t kShtc3CmdSleep = 0xB098;

// CRC-8: polynomial 0x31 (x^8+x^5+x^4+1), init 0xFF, no reflection, no final
// XOR - the exact SHTC3 datasheet definition (same computation as Waveshare's
// Shtc3_CheckCrc, whose 0x131 constant truncates to 0x31 in the uint8_t
// arithmetic it's used in).
inline uint8_t shtc3_crc8(const uint8_t* data, std::size_t len) {
  uint8_t crc = 0xFF;
  for (std::size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                          : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

// Datasheet conversion formulas (no self-heating trim applied here - see
// kShtc3TemperatureTrimC below for that calibration knob).
// T[C]  = -45 + 175 * raw / 65536
// RH[%] =        100 * raw / 65536
inline float shtc3_convert_temperature_c(uint16_t raw) {
  return -45.0f + 175.0f * (static_cast<float>(raw) / 65536.0f);
}

inline float shtc3_convert_humidity_percent(uint16_t raw) {
  return 100.0f * (static_cast<float>(raw) / 65536.0f);
}

// SHTC3's own datasheet operating range; a reading outside this is a
// sensor/bus fault, not a real room, so it must not reach the UI.
inline constexpr float kShtc3MinPlausibleTemperatureC = -40.0f;
inline constexpr float kShtc3MaxPlausibleTemperatureC = 125.0f;
inline constexpr float kShtc3MinPlausibleHumidityPercent = 0.0f;
inline constexpr float kShtc3MaxPlausibleHumidityPercent = 100.0f;

inline bool shtc3_reading_plausible(float temperature_c, float humidity_percent) {
  return temperature_c >= kShtc3MinPlausibleTemperatureC &&
         temperature_c <= kShtc3MaxPlausibleTemperatureC &&
         humidity_percent >= kShtc3MinPlausibleHumidityPercent &&
         humidity_percent <= kShtc3MaxPlausibleHumidityPercent;
}

// raw is the 6-byte T-first reply: T MSB, T LSB, T CRC, RH MSB, RH LSB, RH
// CRC. Returns false - leaving temperature_c/humidity_percent untouched - on
// either CRC mismatch or an implausible result, so a corrupted or garbage
// reading never turns into a fabricated number for a caller to publish.
inline bool shtc3_decode(const uint8_t raw[6], float& temperature_c,
                         float& humidity_percent) {
  if (shtc3_crc8(raw, 2) != raw[2]) return false;
  if (shtc3_crc8(raw + 3, 2) != raw[5]) return false;

  const uint16_t raw_t = static_cast<uint16_t>((raw[0] << 8) | raw[1]);
  const uint16_t raw_rh = static_cast<uint16_t>((raw[3] << 8) | raw[4]);
  const float temperature = shtc3_convert_temperature_c(raw_t);
  const float humidity = shtc3_convert_humidity_percent(raw_rh);
  if (!shtc3_reading_plausible(temperature, humidity)) return false;

  temperature_c = temperature;
  humidity_percent = humidity;
  return true;
}

#ifdef ESP_PLATFORM

// Calibration note: Waveshare's own factory demo subtracts a fixed 4 C from this
// same formula, citing self-heating from sitting next to the ESP32-S3
// module. Left at 0 rather than inherited blindly - self-heating depends on
// enclosure and placement, so this is a per-board trim a bring-up pass
// should set against a reference thermometer, not a constant to copy.
inline constexpr float kShtc3TemperatureTrimC = 0.0f;

// Wakes the sensor, issues a T-first normal-mode measurement, reads back 6
// bytes, puts it back to sleep, and decodes+validates via shtc3_decode()
// (applying kShtc3TemperatureTrimC to the temperature). Uses the device
// handle from board_i2c_add_device(kShtc3Address, ...) - call board_i2c_init()
// and add the device once before the first read.
//
// Returns true only when the whole pipeline succeeds (I2C transport, both
// CRCs, plausibility); false otherwise, leaving temperature_c/
// humidity_percent untouched, so a caller can wire this straight into
// app_core::IndoorData::valid without a separate plausibility check.
bool shtc3_read(float& temperature_c, float& humidity_percent);

#endif  // ESP_PLATFORM

}  // namespace board
