#include "shtc3.hpp"

#include <cmath>
#include <cstdint>

#include "test_support.hpp"

namespace {
bool near(float actual, float expected, float epsilon = 0.001f) {
  return std::fabs(actual - expected) <= epsilon;
}
}  // namespace

// CRC-8 (poly 0x31, init 0xFF) test vectors are the standard examples from
// Sensirion's CRC checksum app note, reused across the SHT2x/SHT3x/SHTC3
// datasheet family.
HOST_TEST(shtc3_crc8_matches_known_good_vectors) {
  const uint8_t a[2] = {0xBE, 0xEF};
  EXPECT_EQ(board::shtc3_crc8(a, 2), 0x92);
  const uint8_t zeros[2] = {0x00, 0x00};
  EXPECT_EQ(board::shtc3_crc8(zeros, 2), 0x81);
  const uint8_t ones[2] = {0xFF, 0xFF};
  EXPECT_EQ(board::shtc3_crc8(ones, 2), 0xAC);
}

HOST_TEST(shtc3_crc8_detects_a_flipped_bit) {
  const uint8_t good[2] = {0xBE, 0xEF};
  const uint8_t corrupted[2] = {0xBE, 0xEE};  // one bit flipped
  EXPECT_TRUE(board::shtc3_crc8(good, 2) != board::shtc3_crc8(corrupted, 2));
}

// Pure conversion formulas at both ends of the 16-bit raw range.
HOST_TEST(shtc3_convert_temperature_c_matches_datasheet_formula_at_endpoints) {
  EXPECT_TRUE(near(board::shtc3_convert_temperature_c(0), -45.0f));
  EXPECT_TRUE(near(board::shtc3_convert_temperature_c(65535), 129.997f));
}

HOST_TEST(shtc3_convert_humidity_percent_matches_datasheet_formula_at_endpoints) {
  EXPECT_TRUE(near(board::shtc3_convert_humidity_percent(0), 0.0f));
  EXPECT_TRUE(near(board::shtc3_convert_humidity_percent(65535), 99.998f));
}

HOST_TEST(shtc3_convert_matches_datasheet_formula_at_midpoint) {
  // raw 0x8000 is exactly half scale: T = -45 + 175*0.5 = 42.5, RH = 50.0.
  EXPECT_EQ(board::shtc3_convert_temperature_c(0x8000), 42.5f);
  EXPECT_EQ(board::shtc3_convert_humidity_percent(0x8000), 50.0f);
}

HOST_TEST(shtc3_reading_plausible_gates_the_datasheet_operating_range) {
  EXPECT_TRUE(board::shtc3_reading_plausible(-40.0f, 0.0f));
  EXPECT_TRUE(board::shtc3_reading_plausible(125.0f, 100.0f));
  EXPECT_TRUE(!board::shtc3_reading_plausible(-40.01f, 50.0f));
  EXPECT_TRUE(!board::shtc3_reading_plausible(125.01f, 50.0f));
  EXPECT_TRUE(!board::shtc3_reading_plausible(20.0f, -0.01f));
  EXPECT_TRUE(!board::shtc3_reading_plausible(20.0f, 100.01f));
}

HOST_TEST(shtc3_decode_succeeds_on_a_valid_midpoint_frame) {
  // raw 0x8000 for both channels; CRC(0x80,0x00) = 0xA2 (see crc8 test above
  // for the polynomial/init this was computed with).
  const uint8_t raw[6] = {0x80, 0x00, 0xA2, 0x80, 0x00, 0xA2};
  float temperature_c = 0.0f;
  float humidity_percent = 0.0f;
  EXPECT_TRUE(board::shtc3_decode(raw, temperature_c, humidity_percent));
  EXPECT_EQ(temperature_c, 42.5f);
  EXPECT_EQ(humidity_percent, 50.0f);
}

HOST_TEST(shtc3_decode_rejects_a_bad_crc_without_touching_the_outputs) {
  // Same payload as the passing test but the temperature CRC byte is wrong;
  // a corrupted byte on the wire must not become a fabricated reading.
  const uint8_t raw[6] = {0x80, 0x00, 0x00, 0x80, 0x00, 0xA2};
  float temperature_c = -999.0f;
  float humidity_percent = -999.0f;
  EXPECT_TRUE(!board::shtc3_decode(raw, temperature_c, humidity_percent));
  EXPECT_EQ(temperature_c, -999.0f);
  EXPECT_EQ(humidity_percent, -999.0f);
}

HOST_TEST(shtc3_decode_rejects_a_physically_impossible_result_despite_a_valid_crc) {
  // raw 0x0000 has a correct CRC (0x81) but converts to -45 C, below the
  // SHTC3 datasheet's -40 C operating floor: a valid transport, invalid
  // physics, must still be rejected as invalid.
  const uint8_t raw[6] = {0x00, 0x00, 0x81, 0x00, 0x00, 0x81};
  float temperature_c = -999.0f;
  float humidity_percent = -999.0f;
  EXPECT_TRUE(!board::shtc3_decode(raw, temperature_c, humidity_percent));
  EXPECT_EQ(temperature_c, -999.0f);
  EXPECT_EQ(humidity_percent, -999.0f);
}
