#pragma once

#ifdef ESP_PLATFORM

#include <esp_err.h>

// Codec/tone output for the board's ES8311 codec (0x18 on the shared I2C
// bus, see board_i2c.hpp) and its I2S TX link (pins in board_pins.hpp) -
// enough to play a short alarm/notification tone through the speaker on the
// MX1.25 header. No microphone input.
//
// Optional module, not part of the native firmware: see
// modules/audio/Kconfig.projbuild (CONFIG_AUDIO_ENABLE). The display,
// clock, sensors, battery, portal, and OTA must build and behave
// identically whether this is on or off, which is why every function here
// exists unconditionally - callers (main/app_main.cpp, the portal's /beep
// route) never need an #ifdef of their own.
namespace audio {

#ifdef CONFIG_AUDIO_ENABLE

// Sets up the I2S TX channel and the ES8311 control path over the shared
// I2C bus. Leaves the amplifier (GPIO46) low and the I2S channel disabled -
// audio_init() never makes sound, only audio_play_tone() does. Idempotent.
//
// A failure here must never be treated as fatal by the caller: this board's
// primary job is the display, and a codec that is unfitted, unresponsive, or
// misconfigured should mean no sound, not no boot.
esp_err_t audio_init();

// Plays a short sine tone: silence, amplifier on, tone (with a few
// milliseconds of fade in/out to avoid a click), amplifier off, silence,
// then stops the I2S channel again. GPIO46 is only ever driven high for the
// duration of this call - see the strapping-pin comment on kAudioAmpEnable
// in board_pins.hpp for why that matters.
//
// Blocks for roughly duration_ms plus the fixed lead-in/lead-out silence -
// prefer audio_play_tone_async() below for anything reachable from the
// HTTP server, which is single-task and cannot serve any other route
// (including /shot and /restart) while a handler is blocked in here.
esp_err_t audio_play_tone(int frequency_hz, int duration_ms);

// Starts audio_play_tone() on its own task and returns immediately, once the
// task exists - not once the tone has played; there is no "finished" signal
// here, only "started" or "refused". Refuses (ESP_ERR_INVALID_STATE) rather
// than queuing or running concurrently if playback is already in progress:
// there is one I2S channel and one codec device, both accessed without any
// internal locking (see esp_codec_dev.c - open()/close() only guard against
// a redundant call with a plain, unprotected bool), so two overlapping
// playbacks would race that state, not queue politely.
esp_err_t audio_play_tone_async(int frequency_hz, int duration_ms);

// 0-100. Applied immediately if the codec happens to be open (it normally
// is not, between tones); otherwise stored and applied at the next
// audio_play_tone(). Default is the volume documented next to
// kOutputVolumePercent in audio.cpp.
void audio_set_volume(int percent);

// Debug-only diagnostic: a fixed staircase (2 kHz stepped through 20/30/40/
// 50% codec volume, then 1 kHz and 3 kHz at 50%, ~400 ms each with ~300 ms
// silence between) plus an ES8311 register-map dump to the log, for telling
// "too quiet", "amplifier not enabled", and "DAC path not producing output"
// apart when a tone plays with no reported error but nothing is heard.
//
// enable_amplifier=false plays the identical staircase without ever raising
// GPIO46 - the decisive functional test for whether the enable line gates
// the amplifier at all, independent of whether its readback can be trusted.
// Audible at the same volume either way means it does not.
//
// Blocks for several seconds - prefer audio_play_diagnostic_sweep_async()
// below for anything reachable from the HTTP server, same reason as
// audio_play_tone_async().
esp_err_t audio_play_diagnostic_sweep(bool enable_amplifier = true);

// Same non-blocking/refuse-if-busy contract as audio_play_tone_async(), for
// audio_play_diagnostic_sweep().
esp_err_t audio_play_diagnostic_sweep_async(bool enable_amplifier = true);

// No tray-indicator registration function here: audio_init() registers this
// module's own icon directly with app_core's tray registry (see
// tray_registry.hpp) as part of its own setup, and write_tone_step() (in
// audio.cpp) sets it active/inactive around GPIO46 itself. Nothing about
// that needs to be in this public header - unlike the wifi_provision
// handler indirection an earlier version of this needed, app_core has no
// dependents that would make a direct call into it circular.

#else  // !CONFIG_AUDIO_ENABLE

// Inline no-ops, not a runtime check: with the option off, audio.cpp is not
// even compiled (see CMakeLists.txt), so there is no codec driver, no I2S
// channel, and no espressif/esp_codec_dev in this build at all. A caller
// that only ever sees these declarations cannot tell the difference from a
// link error, which is the point - one header, one behavior either way.
inline esp_err_t audio_init() { return ESP_ERR_NOT_SUPPORTED; }
inline esp_err_t audio_play_tone(int /*frequency_hz*/, int /*duration_ms*/) {
  return ESP_ERR_NOT_SUPPORTED;
}
inline esp_err_t audio_play_tone_async(int /*frequency_hz*/, int /*duration_ms*/) {
  return ESP_ERR_NOT_SUPPORTED;
}
inline void audio_set_volume(int /*percent*/) {}
inline esp_err_t audio_play_diagnostic_sweep(bool = true) {
  return ESP_ERR_NOT_SUPPORTED;
}
inline esp_err_t audio_play_diagnostic_sweep_async(bool = true) {
  return ESP_ERR_NOT_SUPPORTED;
}

#endif  // CONFIG_AUDIO_ENABLE

}  // namespace audio

#endif  // ESP_PLATFORM
