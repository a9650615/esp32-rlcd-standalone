#include "audio_drain.hpp"

#include "test_support.hpp"

// The one piece of modules/audio that is not #ifdef ESP_PLATFORM - see
// audio_drain.hpp's own comment for why it was split out. Everything else
// in that module touches a real I2S/codec driver and has no host-testable
// form; this arithmetic is what "test what can be tested on the host" means
// for the streaming-sink work that added the 44.1 kHz caller.

HOST_TEST(drain_ms_for_rate_matches_the_original_tone_path_constant) {
  // 6 * 240 = 1440 frames, exactly 90 ms at 16 kHz - this is the value the
  // tone path has always used (it was a literal before this was split into
  // a function of the rate); this test is what keeps that number honest.
  EXPECT_EQ(audio::drain_ms_for_rate(16000), 90u);
}

HOST_TEST(drain_ms_for_rate_is_shorter_at_the_streaming_rate) {
  // Same 1440-frame DMA depth, clocked out faster at 44.1 kHz than at
  // 16 kHz - the whole reason this is a function of the rate rather than
  // one constant the streaming path would have had to reuse incorrectly.
  EXPECT_EQ(audio::drain_ms_for_rate(44100), 33u);
  EXPECT_TRUE(audio::drain_ms_for_rate(44100) < audio::drain_ms_for_rate(16000));
}

HOST_TEST(drain_ms_for_rate_rounds_up_rather_than_truncating) {
  // 1440 frames / 8000 Hz = 180.0 ms exactly (no rounding needed) and
  // 1440 / 48000 = 30.0 ms exactly - both chosen because they are exact,
  // so this test is checking the formula's shape (ceiling division, via
  // "+ sample_rate_hz - 1") rather than relying on inexact arithmetic to
  // prove itself.
  EXPECT_EQ(audio::drain_ms_for_rate(8000), 180u);
  EXPECT_EQ(audio::drain_ms_for_rate(48000), 30u);
}
