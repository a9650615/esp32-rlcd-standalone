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
// ES8311 codec (0x18 on the shared I2C bus) and its I2S link.
inline constexpr gpio_num_t kAudioMclk = GPIO_NUM_16;
inline constexpr gpio_num_t kAudioBclk = GPIO_NUM_9;
// Strapping pin (VDD_SPI voltage select, sampled at reset). The codec drives
// this as an output once I2S starts, same as every other board that wires
// WS/LRCLK here; it only matters if something holds it against the strap
// level *during* reset, which I2S does not.
inline constexpr gpio_num_t kAudioLrclk = GPIO_NUM_45;
inline constexpr gpio_num_t kAudioDout = GPIO_NUM_8;
inline constexpr gpio_num_t kAudioDin = GPIO_NUM_10;  // mic, unused before stage 2
// Strapping pin (ROM message print control, sampled at reset). Unlike LRCLK
// this one is fully under app control rather than a peripheral's, which is
// exactly the danger: audio_play_tone() must drive it high only for the
// duration of a tone and low the rest of the time, including across every
// reset, or a stuck-high amp enable would both mis-strap the next boot and
// leave a battery-powered amplifier burning current forever.
inline constexpr gpio_num_t kAudioAmpEnable = GPIO_NUM_46;
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
static_assert(kAudioMclk == GPIO_NUM_16, "audio MCLK pin changed");
static_assert(kAudioBclk == GPIO_NUM_9, "audio BCLK pin changed");
static_assert(kAudioLrclk == GPIO_NUM_45, "audio LRCLK pin changed");
static_assert(kAudioDout == GPIO_NUM_8, "audio DOUT pin changed");
static_assert(kAudioDin == GPIO_NUM_10, "audio DIN pin changed");
static_assert(kAudioAmpEnable == GPIO_NUM_46, "audio amp enable pin changed");
static_assert(kWidth == 400 && kHeight == 300, "display geometry changed");

}  // namespace board
