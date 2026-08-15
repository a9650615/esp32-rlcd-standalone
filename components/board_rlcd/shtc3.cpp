#include "shtc3.hpp"

#ifdef ESP_PLATFORM

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board_i2c.hpp"

namespace board {
namespace {

constexpr char kTag[] = "shtc3";
// Matches Waveshare's factory 05_I2C_SHTC3 example's SHTC3 device config.
constexpr uint32_t kSclSpeedHz = 400'000;
// Datasheet wake-up time is <=240 us; one tick is the smallest delay above it.
constexpr TickType_t kWakeSettleDelay = pdMS_TO_TICKS(1);
// Datasheet max normal-mode conversion time is 12.1 ms; padded for margin.
constexpr TickType_t kMeasureDelay = pdMS_TO_TICKS(15);

i2c_master_dev_handle_t g_device = nullptr;

bool send_command(uint16_t command) {
  const uint8_t buf[2] = {static_cast<uint8_t>(command >> 8),
                          static_cast<uint8_t>(command & 0xFF)};
  const esp_err_t result = i2c_master_transmit(g_device, buf, sizeof(buf), 100);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "command 0x%04x failed: %s", command, esp_err_to_name(result));
    return false;
  }
  return true;
}

bool ensure_device() {
  if (g_device != nullptr) return true;
  if (board_i2c_init() != ESP_OK) return false;
  return board_i2c_add_device(kShtc3Address, kSclSpeedHz, g_device) == ESP_OK;
}

}  // namespace

bool shtc3_read(float& temperature_c, float& humidity_percent) {
  if (!ensure_device()) return false;

  if (!send_command(kShtc3CmdWakeup)) return false;
  vTaskDelay(kWakeSettleDelay);

  if (!send_command(kShtc3CmdMeasureTFirst)) return false;
  vTaskDelay(kMeasureDelay);

  uint8_t raw[6]{};
  const esp_err_t read_result = i2c_master_receive(g_device, raw, sizeof(raw), 100);
  // Always try to sleep the sensor, even on a failed read, so it doesn't sit
  // awake drawing standby current until the next poll.
  send_command(kShtc3CmdSleep);
  if (read_result != ESP_OK) {
    ESP_LOGW(kTag, "measurement read failed: %s", esp_err_to_name(read_result));
    return false;
  }

  float decoded_temperature = 0.0f;
  float decoded_humidity = 0.0f;
  if (!shtc3_decode(raw, decoded_temperature, decoded_humidity)) {
    ESP_LOGW(kTag, "measurement rejected: CRC mismatch or implausible reading");
    return false;
  }

  temperature_c = decoded_temperature + kShtc3TemperatureTrimC;
  humidity_percent = decoded_humidity;
  return true;
}

}  // namespace board

#endif  // ESP_PLATFORM
