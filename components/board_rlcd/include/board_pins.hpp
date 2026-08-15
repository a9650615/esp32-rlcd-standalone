#pragma once

#include <driver/gpio.h>

namespace board {

inline constexpr gpio_num_t kDisplaySck = GPIO_NUM_11;
inline constexpr gpio_num_t kDisplayMosi = GPIO_NUM_12;
inline constexpr gpio_num_t kDisplayDc = GPIO_NUM_5;
inline constexpr gpio_num_t kDisplayCs = GPIO_NUM_40;
inline constexpr gpio_num_t kDisplayReset = GPIO_NUM_41;
inline constexpr gpio_num_t kDisplayTe = GPIO_NUM_6;
inline constexpr gpio_num_t kKey = GPIO_NUM_18;
inline constexpr gpio_num_t kBoot = GPIO_NUM_0;
// Shared I2C bus: RTC (PCF85063, 0x51), SHTC3 (0x70), and the audio codecs
// all sit on this one bus. See board_i2c.hpp for the single owner.
inline constexpr gpio_num_t kI2cSda = GPIO_NUM_13;
inline constexpr gpio_num_t kI2cScl = GPIO_NUM_14;
// 18650 divider tap; official board docs state a 3x divider, so the raw ADC
// reading needs software conversion (see app_core::battery_millivolts()).
inline constexpr gpio_num_t kBatterySense = GPIO_NUM_4;
inline constexpr int kWidth = 400;
inline constexpr int kHeight = 300;

static_assert(kDisplaySck == GPIO_NUM_11, "display SCK pin changed");
static_assert(kDisplayMosi == GPIO_NUM_12, "display MOSI pin changed");
static_assert(kDisplayDc == GPIO_NUM_5, "display DC pin changed");
static_assert(kDisplayCs == GPIO_NUM_40, "display CS pin changed");
static_assert(kDisplayReset == GPIO_NUM_41, "display reset pin changed");
static_assert(kDisplayTe == GPIO_NUM_6, "display TE pin changed");
static_assert(kKey == GPIO_NUM_18, "KEY pin changed");
static_assert(kBoot == GPIO_NUM_0, "BOOT recovery pin changed");
static_assert(kBatterySense == GPIO_NUM_4, "battery sense pin changed");
static_assert(kI2cSda == GPIO_NUM_13, "shared I2C SDA pin changed");
static_assert(kI2cScl == GPIO_NUM_14, "shared I2C SCL pin changed");
static_assert(kWidth == 400 && kHeight == 300, "display geometry changed");

}  // namespace board
