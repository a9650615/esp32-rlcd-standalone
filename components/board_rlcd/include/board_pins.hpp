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
static_assert(kWidth == 400 && kHeight == 300, "display geometry changed");

}  // namespace board
