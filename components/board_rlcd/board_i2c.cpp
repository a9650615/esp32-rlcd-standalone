#include "board_i2c.hpp"

#ifdef ESP_PLATFORM

#include <esp_log.h>

#include "board_pins.hpp"

namespace board {
namespace {

constexpr char kTag[] = "board_i2c";
i2c_master_bus_handle_t g_bus = nullptr;

}  // namespace

esp_err_t board_i2c_init() {
  if (g_bus != nullptr) return ESP_OK;

  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = kI2cSda;
  bus_config.scl_io_num = kI2cScl;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;

  const esp_err_t result = i2c_new_master_bus(&bus_config, &g_bus);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "shared I2C bus init failed: %s", esp_err_to_name(result));
    g_bus = nullptr;
    return result;
  }
  return ESP_OK;
}

esp_err_t board_i2c_add_device(uint8_t address_7bit, uint32_t scl_speed_hz,
                               i2c_master_dev_handle_t& out_handle) {
  out_handle = nullptr;
  if (g_bus == nullptr) {
    ESP_LOGE(kTag, "board_i2c_add_device called before board_i2c_init");
    return ESP_ERR_INVALID_STATE;
  }

  i2c_device_config_t device_config{};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = address_7bit;
  device_config.scl_speed_hz = scl_speed_hz;
  const esp_err_t result =
      i2c_master_bus_add_device(g_bus, &device_config, &out_handle);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "I2C device add failed for address 0x%02x: %s",
             address_7bit, esp_err_to_name(result));
  }
  return result;
}

}  // namespace board

#endif  // ESP_PLATFORM
