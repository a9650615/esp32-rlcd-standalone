#pragma once

#ifdef ESP_PLATFORM

#include <cstdint>

#include <driver/i2c_master.h>
#include <esp_err.h>

namespace board {

// Owns I2C_NUM_0 (SDA13/SCL14, internal pull-ups) for the life of the app.
// RTC (PCF85063, 0x51) and SHTC3 (0x70) share this bus, so exactly one
// component may create it; everyone else adds a device handle here instead
// of calling i2c_new_master_bus() themselves. Idempotent: safe to call from
// more than one component's init path.
esp_err_t board_i2c_init();

// Adds a 7-bit-address device on the shared bus. Call board_i2c_init()
// first. Logs and returns the esp_err_t on failure.
esp_err_t board_i2c_add_device(uint8_t address_7bit, uint32_t scl_speed_hz,
                               i2c_master_dev_handle_t& out_handle);

}  // namespace board

#endif  // ESP_PLATFORM
