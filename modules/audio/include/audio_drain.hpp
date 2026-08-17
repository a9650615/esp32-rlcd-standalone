#pragma once

#include <cstdint>

// Pure arithmetic, no ESP-IDF dependency - host-testable, unlike the rest of
// this module (audio.hpp/audio.cpp are entirely #ifdef ESP_PLATFORM, since
// they touch real I2S/codec drivers). Split out for the same reason
// components/market keeps market_parse.hpp separate from market.hpp: so a
// unit that used to have exactly one caller, at one fixed sample rate, can
// still be checked once it has two, at two different rates.
namespace audio {

// I2S DMA ring depth - fixed hardware properties of the channel (audio.cpp's
// setup_i2s() configures it with exactly these, once, for the channel's
// entire lifetime; the channel is reused at whatever sample rate a given
// session opens at, but this depth never changes). Explicit rather than left
// at whatever I2S_CHANNEL_DEFAULT_CONFIG happens to default to, because
// drain_ms_for_rate() below has to stay in sync with whatever the channel is
// actually configured with.
inline constexpr uint32_t kDmaDescNum = 6;
inline constexpr uint32_t kDmaFrameNum = 240;

// How long the DMA ring above takes to drain at a given sample rate, in
// milliseconds, rounded up. esp_codec_dev_write() returns once data is
// copied into a free DMA descriptor, not once it has actually been clocked
// out (see audio_codec_data_i2s.c / i2s_common.c), so this is how long to
// wait after the last write before dropping the amplifier or closing the
// channel, at whatever sample rate is actually in use - cutting that wait
// short can clip the tail of a fade-out or trailing silence, which is
// exactly the click this exists to prevent.
//
// One formula, not one constant per caller: the tone path (fixed 16 kHz)
// and the streaming path (44.1 kHz, or whatever a caller opens at) both
// derive their own wait from this, rather than each carrying an
// independently-tuned number that could drift out of sync with the DMA
// config above if it ever changes.
constexpr uint32_t drain_ms_for_rate(uint32_t sample_rate_hz) {
  return (kDmaDescNum * kDmaFrameNum * 1000 + sample_rate_hz - 1) /
        sample_rate_hz;
}

}  // namespace audio
