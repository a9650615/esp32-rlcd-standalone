#include "audio.hpp"

#ifdef ESP_PLATFORM

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <es8311_codec.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <esp_timer.h>

#include "audio_drain.hpp"
#include "board_i2c.hpp"
#include "board_pins.hpp"
#include "tray_registry.hpp"

namespace audio {
namespace {

constexpr char kTag[] = "audio";

constexpr uint32_t kSampleRateHz = 16000;
// esp_codec_dev's I2S data interface rejects an odd channel count outright
// (see audio_codec_data_i2s.c), so mono is not an option even though only
// one physical speaker is wired - both slots carry the same sample.
constexpr int kChannels = 2;

// 50 is a conservative default picked during a night-time bring-up session,
// specifically to avoid startling anyone at 1am while the sweep staircase
// (see kSweepSteps) was first tried on the actual speaker - not a value
// chosen, or validated, for how loud this needs to be to actually do its
// job. It is known to be clearly audible in a quiet room at close range,
// nothing more; an alarm or notification that has to be heard across a room,
// or over background noise, may well need considerably more than this. If
// you are looking at this value to decide whether it needs raising, the
// answer is "probably re-evaluate it, don't assume it was chosen for your
// use case" - audio_set_volume(), or /beep's ?vol= query parameter, is the
// cheap way to do that without a rebuild.
constexpr int kOutputVolumePercent = 50;

// 50% of int16 full scale - the same ceiling /beep-sweep's diagnostic
// staircase is capped at. A full-scale square wave into a coin-sized
// MX1.25 speaker is both the loudest and harshest thing this chain can
// produce; a clean sine at half scale is loud enough to hear clearly
// without either driving the speaker at its worst or startling whoever is
// nearby.
constexpr float kToneAmplitudeScale = 0.5f;
constexpr int16_t kToneAmplitude =
    static_cast<int16_t>(32767.0f * kToneAmplitudeScale);

constexpr int kMinFrequencyHz = 100;
// 6 kHz, not the 8 kHz Nyquist limit that a 16 kHz sample rate actually
// allows: 8000 Hz itself is exactly two samples per cycle - a degenerate
// waveform, not a tone - so this leaves real headroom under Nyquist rather
// than sitting on it.
constexpr int kMaxFrequencyHz = 6000;
constexpr int kMinDurationMs = 50;
constexpr int kMaxDurationMs = 3000;  // safety cap; callers should ask for far less

// Fade in/out inside the tone itself, so the waveform's own start and end
// are already at zero - an abrupt sine start/stop is a step function, and a
// small speaker turns that step into a click louder than the tone.
constexpr int kFadeMs = 5;

// Silence pushed through the (already-enabled) I2S channel immediately
// before the amplifier turns on, and again immediately before it turns off.
// Flipping GPIO46 while the I2S line is idle or mid-transition is what
// produces the loud turn-on/turn-off thump; doing it while known-silent
// samples are already flowing removes that transition.
constexpr int kSilenceLeadMs = 20;
constexpr int kSilenceTrailMs = 20;

// kDmaDescNum/kDmaFrameNum (audio_drain.hpp) are what setup_i2s() below
// configures the channel with; kDrainMs is drain_ms_for_rate() evaluated at
// this module's one fixed tone rate. See audio_drain.hpp's own comment for
// why esp_codec_dev_write() returning is not proof the samples it just
// queued have reached the pin yet, and why the streaming path (a different,
// caller-chosen rate) derives its own wait from the same formula rather than
// reusing this literal constant.
constexpr uint32_t kDrainMs = drain_ms_for_rate(kSampleRateHz);

// ES8311 register map is documented up to 0x45; dumping it is cheap (one
// byte per I2C transaction) and is the only evidence that actually shows
// what the chip's clock manager, DAC mute, and DAC volume registers hold,
// as opposed to what this code believes it configured. See dump_registers().
constexpr uint8_t kRegisterDumpFirst = 0x00;
constexpr uint8_t kRegisterDumpLast = 0x45;
constexpr int kRegistersPerLogLine = 16;

// /beep-sweep's diagnostic staircase: a fixed frequency stepped up through
// codec volume, then two more frequencies at the loudest step, so an
// operator listening can report the first step that was actually audible
// and separate "too quiet" from "nothing reaches the speaker at all".
struct SweepStep {
  int frequency_hz;
  int volume_percent;
};
// Ceiling brought down from 80% after the first hardware run: 20/30/40/50%
// covers the range that matters now that 2 kHz at 20% was already audible
// and 80% read as "a bit loud" - see kOutputVolumePercent for where the
// compiled-in /beep default landed instead.
constexpr SweepStep kSweepSteps[] = {
    {2000, 20}, {2000, 30}, {2000, 40}, {2000, 50},
    {1000, 50}, {3000, 50},
};
constexpr int kSweepToneMs = 400;
constexpr int kSweepGapMs = 300;  // silence between steps, amp off, so steps are distinguishable

i2s_chan_handle_t g_tx_handle = nullptr;
esp_codec_dev_handle_t g_codec_dev = nullptr;
int g_volume_percent = kOutputVolumePercent;
// Separate from the codec's own I2C control handle (owned internally by
// audio_codec_new_i2c_ctrl in setup_codec()): this one exists purely so
// dump_registers() can issue plain register reads without reaching into
// esp_codec_dev's opaque state. Two device handles at the same address on
// one shared bus is fine - each I2C transaction carries its own address and
// they're serialized by the bus's own lock, the same way board_i2c_scan's
// repeated i2c_master_probe() calls already share the bus with everyone else.
i2c_master_dev_handle_t g_diag_i2c_device = nullptr;

// This module's own tray icon, registered once with app_core's registry
// (see tray_registry.hpp) and never referred to by name anywhere in core -
// core only ever sees the bytes below, not "audio" or "speaker". 16x12,
// 1 bit/pixel, row-major MSB-first, each row padded to a whole byte
// (stride = (kIconWidth+7)/8 = 2 bytes) - see TrayIndicatorBitmap's own
// comment in tray_registry.hpp for why this exact layout.
//
// Built once at runtime, in audio_init(), rather than hand-encoded as a
// byte literal: this is the same body-plus-two-concentric-arcs design the
// old hand-drawn LVGL speaker icon used (see this file's git history),
// just rasterized into bits instead of LVGL sub-objects - a plain
// distance-from-centre test per pixel needs a loop, not a lookup table, and
// this runs exactly once. This is also what "supplies its own icon as
// data, not code" means in practice: this module includes nothing
// LVGL-specific to draw itself with any more.
constexpr int kIconWidth = 16;
constexpr int kIconHeight = 12;
constexpr int kIconStride = (kIconWidth + 7) / 8;
uint8_t g_icon_bitmap[kIconStride * kIconHeight];
app_core::TrayIndicatorHandle g_tray_indicator;

void set_icon_pixel(int x, int y) {
  if (x < 0 || x >= kIconWidth || y < 0 || y >= kIconHeight) return;
  g_icon_bitmap[y * kIconStride + x / 8] |=
      static_cast<uint8_t>(0x80 >> (x % 8));
}

void build_icon_bitmap() {
  std::fill(g_icon_bitmap, g_icon_bitmap + sizeof(g_icon_bitmap), uint8_t{0});
  // Body: left ~40% of width, ~5/7 of height, vertically centred - the same
  // proportions the old hand-drawn icon's rectangle used.
  const int body_width = std::max(3, kIconWidth * 2 / 5);
  const int body_height = std::max(4, kIconHeight * 5 / 7);
  const int body_y = (kIconHeight - body_height) / 2;
  for (int y = body_y; y < body_y + body_height; ++y) {
    for (int x = 0; x < body_width; ++x) set_icon_pixel(x, y);
  }
  // Two concentric arcs to the right of the body ("sound waves"): a plain
  // distance-from-centre band test per pixel, since a raw bitmap has no
  // LVGL clip container to lean on for the "only draw the right half"
  // trick the old LVGL-object version used.
  const int centre_x = body_width + 1;
  const int centre_y = kIconHeight / 2;
  const int max_radius = std::min(kIconWidth - centre_x, centre_y);
  constexpr int kWaveCount = 2;
  constexpr int kStrokeWidth = 1;  // pixels; this bitmap is tiny, keep it thin
  for (int i = 0; i < kWaveCount; ++i) {
    const int radius = max_radius * (i + 1) / kWaveCount;
    const int inner = std::max(0, radius - kStrokeWidth);
    for (int y = 0; y < kIconHeight; ++y) {
      for (int x = centre_x; x < kIconWidth; ++x) {
        const int dx = x - centre_x;
        const int dy = y - centre_y;
        const int dist_sq = dx * dx + dy * dy;
        if (dist_sq <= radius * radius && dist_sq > inner * inner) {
          set_icon_pixel(x, y);
        }
      }
    }
  }
}

void configure_amp_gpio() {
  // Write the level before switching the pin to an output driver, and again
  // right after: GPIO46 is a boot-strapping pin (ROM message print control),
  // and this is a battery-powered device with active battery-drain
  // tracking, so it must never be observed high except while a tone is
  // actually playing.
  gpio_set_level(board::kAudioAmpEnable, 0);
  gpio_config_t amp_config{};
  amp_config.intr_type = GPIO_INTR_DISABLE;
  // GPIO_MODE_INPUT_OUTPUT, not GPIO_MODE_OUTPUT: confirmed by reading
  // esp_driver_gpio/src/gpio.c's gpio_config() - when GPIO_MODE_DEF_INPUT is
  // not set in `mode`, it calls gpio_input_disable() on the pin, and
  // gpio_get_level() reads the (now-frozen, effectively meaningless) input
  // path rather than the driven output level. A plain GPIO_MODE_OUTPUT pin's
  // own readback of itself cannot be trusted; this mode keeps the output
  // driver while also enabling the input path so gpio_get_level() reflects
  // what is actually on the pin.
  amp_config.mode = GPIO_MODE_INPUT_OUTPUT;
  amp_config.pin_bit_mask = 1ULL << board::kAudioAmpEnable;
  amp_config.pull_down_en = GPIO_PULLDOWN_ENABLE;
  amp_config.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&amp_config);
  gpio_set_level(board::kAudioAmpEnable, 0);
}

esp_err_t setup_i2s() {
  i2s_chan_config_t chan_config =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  // Set explicitly, matching kDmaDescNum/kDmaFrameNum above, rather than
  // trusting the macro's own defaults to stay in sync with kDrainMs.
  chan_config.dma_desc_num = kDmaDescNum;
  chan_config.dma_frame_num = kDmaFrameNum;
  esp_err_t result = i2s_new_channel(&chan_config, &g_tx_handle, nullptr);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "I2S channel allocation failed: %s",
             esp_err_to_name(result));
    return result;
  }

  i2s_std_config_t std_config = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRateHz),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
          I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = board::kAudioMclk,
              .bclk = board::kAudioBclk,
              .ws = board::kAudioLrclk,
              .dout = board::kAudioDout,
              .din = I2S_GPIO_UNUSED,
              .invert_flags = {.mclk_inv = false,
                               .bclk_inv = false,
                               .ws_inv = false},
          },
  };
  result = i2s_channel_init_std_mode(g_tx_handle, &std_config);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "I2S standard-mode init failed: %s",
             esp_err_to_name(result));
  }
  return result;
}

esp_err_t setup_codec() {
  // bus_handle, not a port number: this hands the codec's I2C control
  // interface the bus board_i2c already owns instead of letting it open a
  // second I2C_NUM_0 master, which the new-style driver would refuse anyway.
  audio_codec_i2c_cfg_t i2c_config{};
  i2c_config.port = I2C_NUM_0;
  i2c_config.addr = ES8311_CODEC_DEFAULT_ADDR;
  i2c_config.bus_handle = board::board_i2c_bus_handle();
  if (i2c_config.bus_handle == nullptr) {
    ESP_LOGE(kTag, "shared I2C bus not ready; call board_i2c_init() first");
    return ESP_ERR_INVALID_STATE;
  }
  const audio_codec_ctrl_if_t* ctrl_if = audio_codec_new_i2c_ctrl(&i2c_config);
  if (ctrl_if == nullptr) {
    ESP_LOGE(kTag, "ES8311 control interface unavailable (codec absent?)");
    return ESP_FAIL;
  }

  audio_codec_i2s_cfg_t i2s_config{};
  i2s_config.port = I2S_NUM_0;
  i2s_config.tx_handle = g_tx_handle;
  i2s_config.rx_handle = nullptr;
  const audio_codec_data_if_t* data_if = audio_codec_new_i2s_data(&i2s_config);
  if (data_if == nullptr) {
    ESP_LOGE(kTag, "I2S data interface unavailable");
    return ESP_FAIL;
  }

  es8311_codec_cfg_t codec_config{};
  codec_config.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
  codec_config.ctrl_if = ctrl_if;
  // No gpio_if and pa_pin left at its unused sentinel: the amplifier enable
  // is sequenced by hand in audio_play_tone() (silence, then GPIO46, then
  // tone) to avoid a turn-on pop, which the codec driver's own open()/
  // close()-tied PA control cannot do.
  codec_config.gpio_if = nullptr;
  codec_config.pa_pin = -1;
  codec_config.use_mclk = true;
  const audio_codec_if_t* codec_if = es8311_codec_new(&codec_config);
  if (codec_if == nullptr) {
    ESP_LOGE(kTag, "ES8311 codec interface unavailable");
    return ESP_FAIL;
  }

  esp_codec_dev_cfg_t dev_config{};
  dev_config.dev_type = ESP_CODEC_DEV_TYPE_OUT;
  dev_config.codec_if = codec_if;
  dev_config.data_if = data_if;
  g_codec_dev = esp_codec_dev_new(&dev_config);
  if (g_codec_dev == nullptr) {
    ESP_LOGE(kTag, "codec device handle unavailable");
    return ESP_FAIL;
  }
  return ESP_OK;
}

// One interleaved-stereo sample count worth of silence, used as the lead-in
// and lead-out around the amplifier switching. Takes the sample rate
// explicitly, not kSampleRateHz - the streaming path uses this too, at
// whatever rate a session actually opened at.
std::vector<int16_t> make_silence(int duration_ms, uint32_t sample_rate_hz) {
  const size_t frames = static_cast<size_t>(sample_rate_hz) * duration_ms / 1000;
  return std::vector<int16_t>(frames * kChannels, 0);
}

// A sine tone at kToneAmplitude with a linear fade in/out over kFadeMs at
// each end, so the buffer itself starts and ends at zero.
std::vector<int16_t> make_tone(int frequency_hz, int duration_ms) {
  const size_t frames = static_cast<size_t>(kSampleRateHz) * duration_ms / 1000;
  const size_t fade_frames =
      std::min(frames / 2, static_cast<size_t>(kSampleRateHz) * kFadeMs / 1000);
  std::vector<int16_t> buffer(frames * kChannels);
  constexpr float kTwoPi = 6.283185307f;
  const float step = kTwoPi * static_cast<float>(frequency_hz) /
                     static_cast<float>(kSampleRateHz);
  for (size_t frame = 0; frame < frames; ++frame) {
    float envelope = 1.0f;
    if (frame < fade_frames) {
      envelope = static_cast<float>(frame) / static_cast<float>(fade_frames);
    } else if (frame >= frames - fade_frames) {
      envelope = static_cast<float>(frames - 1 - frame) /
                 static_cast<float>(fade_frames);
    }
    const int16_t sample = static_cast<int16_t>(
        kToneAmplitude * envelope * std::sin(step * static_cast<float>(frame)));
    buffer[frame * kChannels] = sample;
    buffer[frame * kChannels + 1] = sample;
  }
  return buffer;
}

// Reads back ES8311 registers 0x00-0x45 over the shared I2C bus and logs
// them as hex, 16 per line. This is the one piece of evidence that shows
// what the codec's clock manager, DAC mute and DAC volume registers
// actually hold - I2C control answering (the codec opens without error)
// only proves the control path, not that the DAC's analog output path is
// actually enabled.
void dump_registers() {
  if (g_diag_i2c_device == nullptr) {
    // 7-bit form: ES8311_CODEC_DEFAULT_ADDR is the 8-bit (write) address
    // esp_codec_dev's own I2C control interface expects.
    const esp_err_t result = board::board_i2c_add_device(
        ES8311_CODEC_DEFAULT_ADDR >> 1, 100'000, g_diag_i2c_device);
    if (result != ESP_OK) {
      ESP_LOGW(kTag, "register dump: could not open a diagnostic I2C handle: %s",
               esp_err_to_name(result));
      return;
    }
  }

  for (int base = kRegisterDumpFirst; base <= kRegisterDumpLast;
       base += kRegistersPerLogLine) {
    const int end = std::min(base + kRegistersPerLogLine - 1,
                             static_cast<int>(kRegisterDumpLast));
    char line[3 * kRegistersPerLogLine + 1] = {};
    int used = 0;
    for (int reg = base; reg <= end; ++reg) {
      uint8_t address = static_cast<uint8_t>(reg);
      uint8_t value = 0xFF;
      if (i2c_master_transmit_receive(g_diag_i2c_device, &address, sizeof(address),
                                      &value, sizeof(value), 100) != ESP_OK) {
        value = 0xFF;
      }
      const int written =
          std::snprintf(line + used, sizeof(line) - used, " %02x", value);
      if (written <= 0 || written >= static_cast<int>(sizeof(line)) - used) break;
      used += written;
    }
    ESP_LOGI(kTag, "ES8311 regs 0x%02x-0x%02x:%s", base, end, line);
  }
}

// Plays lead-in silence, the tone itself, and lead-out silence around the
// amplifier, per the drain-safe sequence documented on kDrainMs. Assumes
// the codec is already open at the sample rate/format this module uses.
// Logs GPIO46's actual driven level right after raising and right after
// dropping it - now trustworthy, since configure_amp_gpio() enables the
// input path too (see its comment). Returns false if any write failed, but
// always leaves the amplifier guaranteed low.
//
// enable_amplifier=false runs the identical audio path - same writes, same
// codec state, same timing - but never touches GPIO46 at all. This is the
// other half of telling "the readback lied" apart from "the enable line
// does not gate anything": if a tone is just as audible with the enable
// line never raised, the amplifier is not actually controlled by this pin.
bool write_tone_step(int frequency_hz, int duration_ms,
                     bool enable_amplifier = true) {
  std::vector<int16_t> lead = make_silence(kSilenceLeadMs, kSampleRateHz);
  std::vector<int16_t> tone = make_tone(frequency_hz, duration_ms);
  std::vector<int16_t> trail = make_silence(kSilenceTrailMs, kSampleRateHz);

  bool ok = true;
  int result = esp_codec_dev_write(g_codec_dev, lead.data(),
                                   static_cast<int>(lead.size() * sizeof(int16_t)));
  if (result != ESP_CODEC_DEV_OK) {
    ESP_LOGW(kTag, "tone lead-in write failed: %d", result);
    ok = false;
  }

  if (ok && enable_amplifier) {
    // Before the amplifier actually goes high - the tray's indicator (see
    // tray_registry.hpp) is a statement about the speaker, not about a
    // request to use it. Logged unconditionally, on the sending side: the
    // receiving side (ui_tray, render_shared.cpp) already logs every state
    // it observes, but that alone could not tell "the registry was never
    // told" apart from "it was told and nothing downstream noticed" - the
    // second of those turned out to be the real bug once, and finding it
    // cost three hardware rounds because only one side of this call was
    // instrumented. set_tray_indicator_active() itself logs loudly if
    // g_tray_indicator turns out to be invalid (registry full, or
    // audio_init() never reached registration) - this line is what a future
    // failure needs on top of that: proof audio.cpp actually made the call
    // at all, and with which handle.
    ESP_LOGI(kTag, "tray indicator: requesting slot %d active=true",
             static_cast<int>(g_tray_indicator.slot));
    app_core::set_tray_indicator_active(g_tray_indicator, true);
    gpio_set_level(board::kAudioAmpEnable, 1);
    ESP_LOGI(kTag, "GPIO46 (amp enable) reads %d after raising",
             gpio_get_level(board::kAudioAmpEnable));
  }

  if (ok) {
    result = esp_codec_dev_write(g_codec_dev, tone.data(),
                                 static_cast<int>(tone.size() * sizeof(int16_t)));
    if (result != ESP_CODEC_DEV_OK) {
      ESP_LOGW(kTag, "tone write failed: %d", result);
      ok = false;
    }
  }

  if (ok) {
    result = esp_codec_dev_write(g_codec_dev, trail.data(),
                                 static_cast<int>(trail.size() * sizeof(int16_t)));
    if (result != ESP_CODEC_DEV_OK) {
      ESP_LOGW(kTag, "tone trail-out write failed: %d", result);
      ok = false;
    }
  }

  // See kDrainMs: esp_codec_dev_write() returning does not mean the samples
  // it just queued have reached the pin yet. Waiting here, before the
  // amplifier drops, is what keeps the tail of the fade-out and the
  // trailing silence from being cut off mid-buffer.
  vTaskDelay(pdMS_TO_TICKS(kDrainMs));

  // Unconditional, regardless of enable_amplifier: cheap, and it is the one
  // guarantee this function makes on every exit path, including one where
  // GPIO46 was never touched in the first place.
  gpio_set_level(board::kAudioAmpEnable, 0);
  if (enable_amplifier) {
    ESP_LOGI(kTag, "GPIO46 (amp enable) reads %d after dropping",
             gpio_get_level(board::kAudioAmpEnable));
    ESP_LOGI(kTag, "tray indicator: requesting slot %d active=false",
             static_cast<int>(g_tray_indicator.slot));
    app_core::set_tray_indicator_active(g_tray_indicator, false);
  }

  return ok;
}

}  // namespace

esp_err_t audio_init() {
  if (g_codec_dev != nullptr) return ESP_OK;

  // Registered unconditionally, before anything hardware-dependent below
  // that could fail: the tray reserves this module's icon slot regardless
  // of whether the codec turns out to be present, so the tray's layout
  // does not shift depending on what hardware happens to be fitted. A full
  // registry (kMaxTrayIndicators slots already claimed by other modules)
  // is the only way this does not succeed -
  // app_core::set_tray_indicator_active() is already a documented no-op
  // for an invalid handle, so nothing later needs its own extra check.
  if (!g_tray_indicator.valid()) {
    build_icon_bitmap();
    g_tray_indicator = app_core::register_tray_indicator(
        {g_icon_bitmap, static_cast<uint8_t>(kIconWidth),
         static_cast<uint8_t>(kIconHeight)});
    if (!g_tray_indicator.valid()) {
      ESP_LOGW(kTag, "tray indicator registration failed: registry full");
    }
  }

  // GPIO46 goes to a known-low output before anything else touches it - if
  // any step below fails, the amplifier must still be guaranteed off.
  configure_amp_gpio();

  esp_err_t result = board::board_i2c_init();
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "audio init: shared I2C bus unavailable: %s",
             esp_err_to_name(result));
    return result;
  }

  result = setup_i2s();
  if (result != ESP_OK) return result;

  result = setup_codec();
  if (result != ESP_OK) {
    if (g_tx_handle != nullptr) {
      i2s_del_channel(g_tx_handle);
      g_tx_handle = nullptr;
    }
    return result;
  }

  ESP_LOGI(kTag, "ES8311 codec ready, output volume=%d%%", g_volume_percent);
  return ESP_OK;
}

esp_err_t audio_play_tone(int frequency_hz, int duration_ms) {
  if (g_codec_dev == nullptr) {
    ESP_LOGW(kTag, "audio_play_tone called before a successful audio_init()");
    return ESP_ERR_INVALID_STATE;
  }

  const int clamped_frequency =
      std::clamp(frequency_hz, kMinFrequencyHz, kMaxFrequencyHz);
  const int clamped_duration =
      std::clamp(duration_ms, kMinDurationMs, kMaxDurationMs);
  if (clamped_frequency != frequency_hz || clamped_duration != duration_ms) {
    ESP_LOGW(kTag, "tone request clamped to %d Hz / %d ms (asked %d Hz / %d ms)",
             clamped_frequency, clamped_duration, frequency_hz, duration_ms);
  }

  esp_codec_dev_sample_info_t sample_info{};
  sample_info.bits_per_sample = 16;
  sample_info.channel = kChannels;
  sample_info.sample_rate = kSampleRateHz;

  int result = esp_codec_dev_open(g_codec_dev, &sample_info);
  if (result != ESP_CODEC_DEV_OK) {
    ESP_LOGW(kTag, "codec open failed: %d", result);
    return ESP_FAIL;
  }
  // Re-applied on every open: nothing here assumes the codec remembers a
  // volume it was given on a previous, since-closed session.
  esp_codec_dev_set_out_vol(g_codec_dev, g_volume_percent);

  const bool ok = write_tone_step(clamped_frequency, clamped_duration);

  esp_codec_dev_close(g_codec_dev);
  return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t audio_play_diagnostic_sweep(bool enable_amplifier) {
  if (g_codec_dev == nullptr) {
    ESP_LOGW(kTag,
             "audio_play_diagnostic_sweep called before a successful audio_init()");
    return ESP_ERR_INVALID_STATE;
  }

  esp_codec_dev_sample_info_t sample_info{};
  sample_info.bits_per_sample = 16;
  sample_info.channel = kChannels;
  sample_info.sample_rate = kSampleRateHz;

  int open_result = esp_codec_dev_open(g_codec_dev, &sample_info);
  if (open_result != ESP_CODEC_DEV_OK) {
    ESP_LOGW(kTag, "diagnostic sweep: codec open failed: %d", open_result);
    return ESP_FAIL;
  }

  // Cause 3 (DAC path not actually enabled) needs to be told apart from
  // causes 1/2, and this is the evidence that does it: what the codec's
  // registers actually hold, not what setup_codec() believes it wrote.
  dump_registers();

  constexpr size_t kSweepStepCount = sizeof(kSweepSteps) / sizeof(kSweepSteps[0]);
  bool ok = true;
  for (size_t step = 0; step < kSweepStepCount && ok; ++step) {
    const SweepStep& current = kSweepSteps[step];
    ESP_LOGI(kTag, "sweep step %u: %d Hz at %d%% (amplifier %s)",
             static_cast<unsigned>(step + 1), current.frequency_hz,
             current.volume_percent,
             enable_amplifier ? "enabled" : "bypassed");
    esp_codec_dev_set_out_vol(g_codec_dev, current.volume_percent);
    ok = write_tone_step(current.frequency_hz, kSweepToneMs, enable_amplifier);
    if (!ok) break;

    // Silence between steps, amplifier already low (write_tone_step's own
    // cleanup), so the operator hears distinct steps rather than one slide.
    std::vector<int16_t> gap = make_silence(kSweepGapMs, kSampleRateHz);
    esp_codec_dev_write(g_codec_dev, gap.data(),
                        static_cast<int>(gap.size() * sizeof(int16_t)));
  }

  // Restore the persistent default so a plain /beep afterward behaves as
  // documented rather than inheriting whatever step played last.
  esp_codec_dev_set_out_vol(g_codec_dev, g_volume_percent);
  esp_codec_dev_close(g_codec_dev);

  return ok ? ESP_OK : ESP_FAIL;
}

namespace {

// One playback at a time, refused rather than queued if one is already
// running - a tone, a sweep, and (see audio_stream_open() below) a
// streaming session all draw on this same one I2S channel and one codec
// device, so all three share this one guard. Not decorative: esp_codec_dev's
// own open()/close() (see esp_codec_dev.c in the managed component) guard
// against a redundant call only with a plain bool on the codec_dev_t struct -
// there is no mutex anywhere in that struct. Two genuinely overlapping
// playbacks (e.g. two requests reaching this module from separate tasks at
// once) would race that bool with no protection at all; this closes that
// off by construction rather than leaving it as a latent, hard-to-reproduce
// bug.
//
// A tone/sweep requested while a stream is open is refused exactly like two
// overlapping tones would be - claim_playback_slot() cannot tell why the
// slot is taken, only that it is. A stream is expected to run for an entire
// session (far longer than a tone), so this is a real, not theoretical,
// case: whoever owns the stream (audio_stream_close()) must give the slot
// back when the session ends, or nothing else in this module can ever play
// again.
//
// This is NOT the explanation for an
// "i2s_common: i2s_channel_disable(...): the channel has not been enabled
// yet" log line seen on hardware - that turned out to be unrelated to
// concurrency (confirmed on a single-caller, first-tone-after-boot capture,
// which this guard cannot touch either way). See the "channel has not been
// enabled yet" section in modules/audio/README.md for the real, confirmed
// mechanism: benign upstream noise from esp_codec_dev's own defensive
// disable-before-reconfigure in _i2s_data_set_fmt(), not a bug here.
//
// A binary semaphore used as a non-blocking try-lock (xSemaphoreTake(...,
// 0)) is the right size for guarding against actual overlap: it either
// claims the slot immediately or refuses immediately, which is "refuse
// cleanly", not "block until free" (that would just turn the HTTP task's
// blocking problem into a task-pool blocking problem).
SemaphoreHandle_t g_playback_busy = nullptr;
constexpr uint32_t kPlaybackTaskStackBytes = 4096;
// I2S and I2C only - audio_play_tone()/audio_play_diagnostic_sweep() never
// touch flash, so this stack can live in PSRAM (xTaskCreateWithCaps below)
// rather than costing internal DRAM, which this board is short of and
// PSRAM (7-8 MB free) is not. Confirmed against
// FREERTOS_TASK_CREATE_ALLOW_EXT_MEM's own Kconfig help text (freertos/
// Kconfig): the one universal rule for every target is "never accessed
// while the cache is disabled" - true here, since nothing in this task
// writes flash - and the ESP32-only caution about ROM/Bluetooth/Wi-Fi calls
// does not apply on this board's ESP32-S3 (that option defaults to n only
// for IDF_TARGET_ESP32, y everywhere else, this project's sdkconfig
// included). Still an on-demand task, not a permanent one - see
// main/app_main.cpp's update_check_task for why permanent was the wrong
// fix and PSRAM is the right one.
constexpr UBaseType_t kPlaybackTaskCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

// Lazily created on first use rather than at audio_init() time - nothing
// needs it before the first play request, and an audio_init() that fails
// partway must not leave a semaphore allocated for a codec that was never
// actually set up.
bool claim_playback_slot() {
  if (g_playback_busy == nullptr) {
    g_playback_busy = xSemaphoreCreateBinary();
    if (g_playback_busy == nullptr) return false;
    xSemaphoreGive(g_playback_busy);  // seed with one token: "available"
  }
  return xSemaphoreTake(g_playback_busy, 0) == pdTRUE;
}

struct ToneRequest {
  int frequency_hz;
  int duration_ms;
};

// Owns the request the caller handed over (same pattern as ota::pull_task),
// runs the existing blocking audio_play_tone() on its own task so the HTTP
// handler that started it is free immediately, then releases the busy slot
// and deletes itself - vTaskDeleteWithCaps(NULL), not vTaskDelete(NULL): a
// task created by xTaskCreateWithCaps() must be torn down by the matching
// *WithCaps deleter, which frees the PSRAM stack buffer plain vTaskDelete()
// does not know about (self-deletion is explicitly supported - see
// idf_additions.c's prvTaskDeleteWithCapsTask()).
void tone_task(void* argument) {
  std::unique_ptr<ToneRequest> request(static_cast<ToneRequest*>(argument));
  audio_play_tone(request->frequency_hz, request->duration_ms);
  xSemaphoreGive(g_playback_busy);
  vTaskDeleteWithCaps(nullptr);
}

struct SweepRequest {
  bool enable_amplifier;
};

void sweep_task(void* argument) {
  std::unique_ptr<SweepRequest> request(static_cast<SweepRequest*>(argument));
  audio_play_diagnostic_sweep(request->enable_amplifier);
  xSemaphoreGive(g_playback_busy);
  vTaskDeleteWithCaps(nullptr);
}

// --- Streaming: one open codec/I2S session held across an entire call,
// rather than one generated buffer per call. The eventual consumer is
// modules/airplay's RAOP receiver (not wired up yet - see audio.hpp's own
// comment on audio_stream_open()), which hands over PCM in chunks,
// indefinitely, instead of handing over one fixed-length buffer up front.
//
// Deliberately NOT built on top of write_tone_step()/audio_play_tone(): a
// tone's lifetime is one call, with its total duration known before the
// first byte is written; a stream's lifetime is a session of unknown
// length, arriving from a task this module does not own, and ending in
// ways a tone never has to consider (the caller simply stops calling, or
// its task dies). Forcing both shapes through one abstraction would have
// meant either bending the tone path - the one thing in this module
// verified across several hardware rounds already - around a session
// concept it does not need, or growing the streaming path around
// assumptions (a known total duration) that do not hold for it. The two
// paths share the DMA-drain formula (audio_drain.hpp) and the exact GPIO/
// tray sequencing convention (see write_tone_step() vs. the functions
// below), duplicated in a handful of lines rather than factored into a
// third abstraction neither one fully fits.
//
// g_stream_mutex is a genuinely different lock from g_playback_busy above:
// g_playback_busy is held for an entire session (claimed at
// audio_stream_open(), given back at audio_stream_close() or by the
// watchdog) to keep a tone/sweep from running at the same time as a
// stream; g_stream_mutex is a short-lived critical-section lock around
// this module's own stream state and hardware calls, needed because the
// watchdog timer below runs on the esp_timer task, not the caller's -
// without it, a force-close firing at the exact moment a real write is in
// flight could tear down state out from under it.
SemaphoreHandle_t g_stream_mutex = nullptr;
esp_timer_handle_t g_stream_watchdog = nullptr;
bool g_stream_open = false;
// Set once real audio starts flowing (see audio_stream_write()), not at
// open() - opening a stream that never receives a chunk must never touch
// GPIO46 or the tray at all.
bool g_stream_amp_active = false;
uint32_t g_stream_sample_rate = 0;

// A stream is "abandoned" - the writer task died, or simply stopped
// calling audio_stream_write() without ever calling audio_stream_close() -
// rather than merely idle.
//
// The longest legitimate gap is not between two chunks mid-stream, it is
// between open() and the FIRST chunk. RAOP opens the sink as soon as the
// sender says RECORD, then holds every frame until its scheduled playtime
// arrives; that hold is the sender's declared latency, capped at
// MAX_LATENCY = 120 * 44100 * 2 / 100 = 105,840 frames = 2.4 s (see
// rtp.c). At 2000 ms this watchdog fired during that hold on every single
// AirPlay session, closed the stream before one sample was written, and
// made a working receiver look like a silent one. Anything below ~2.4 s is
// not a tuning choice, it is a guaranteed failure.
constexpr uint32_t kStreamWatchdogTimeoutMs = 5000;

// Ends the session: trailing silence, the same drain wait
// write_tone_step() uses (here at whatever rate the stream opened at, via
// drain_ms_for_rate()), amplifier and tray indicator cleared, codec closed,
// busy slot released. Safe to call when no stream is open (checked first) -
// both audio_stream_close() and the watchdog below rely on that. Caller
// must already hold g_stream_mutex.
void close_stream_locked() {
  if (!g_stream_open) return;
  if (g_stream_watchdog != nullptr) esp_timer_stop(g_stream_watchdog);
  if (g_stream_amp_active) {
    std::vector<int16_t> trail = make_silence(kSilenceTrailMs, g_stream_sample_rate);
    esp_codec_dev_write(g_codec_dev, trail.data(),
                        static_cast<int>(trail.size() * sizeof(int16_t)));
    vTaskDelay(pdMS_TO_TICKS(drain_ms_for_rate(g_stream_sample_rate)));
    // Unconditional once amp_active is true, on every path that reaches
    // here - the same guarantee write_tone_step() makes for the tone path:
    // this line is what actually satisfies "the amplifier ends low on
    // every exit path", not merely the intent to reach it.
    gpio_set_level(board::kAudioAmpEnable, 0);
    ESP_LOGI(kTag, "audio stream: GPIO46 (amp enable) reads %d after dropping",
             gpio_get_level(board::kAudioAmpEnable));
    ESP_LOGI(kTag, "tray indicator: requesting slot %d active=false",
             static_cast<int>(g_tray_indicator.slot));
    app_core::set_tray_indicator_active(g_tray_indicator, false);
  }
  esp_codec_dev_close(g_codec_dev);
  g_stream_open = false;
  g_stream_amp_active = false;
  // Balances whichever of audio_stream_open()/claim_playback_slot() first
  // claimed this - always exactly one give per one take, since this only
  // runs at all when g_stream_open was true, which only becomes true after
  // a successful claim.
  xSemaphoreGive(g_playback_busy);
}

// Runs on the esp_timer task, not the stream's own caller - see
// g_stream_mutex's own comment above for why this takes it before touching
// anything. A bounded wait, not portMAX_DELAY: this is a one-shot timer
// firing because writes stopped, so nothing should be holding the mutex
// for long; if a real write is somehow still in flight, that write's own
// audio_stream_write() call already re-armed the watchdog, so skipping
// this particular firing loses nothing.
void stream_watchdog_fired(void*) {
  if (g_stream_mutex == nullptr) return;
  if (xSemaphoreTake(g_stream_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
  if (g_stream_open) {
    ESP_LOGW(kTag,
             "audio stream: no write for %u ms; treating it as abandoned and "
             "closing it",
             static_cast<unsigned>(kStreamWatchdogTimeoutMs));
    close_stream_locked();
  }
  xSemaphoreGive(g_stream_mutex);
}

// Lazily created on first stream use, same reasoning as g_playback_busy
// above.
bool ensure_stream_primitives() {
  if (g_stream_mutex == nullptr) {
    g_stream_mutex = xSemaphoreCreateMutex();
    if (g_stream_mutex == nullptr) return false;
  }
  if (g_stream_watchdog == nullptr) {
    const esp_timer_create_args_t args = {
        .callback = &stream_watchdog_fired,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "audio_stream_wd",
    };
    if (esp_timer_create(&args, &g_stream_watchdog) != ESP_OK) return false;
  }
  return true;
}

// (Re)starts the abandoned-stream countdown; called at open and on every
// write, so it only ever fires after a genuine gap.
void arm_stream_watchdog() {
  esp_timer_stop(g_stream_watchdog);  // harmless if not currently running
  esp_timer_start_once(g_stream_watchdog,
                       static_cast<uint64_t>(kStreamWatchdogTimeoutMs) * 1000);
}

}  // namespace

esp_err_t audio_play_tone_async(int frequency_hz, int duration_ms) {
  if (g_codec_dev == nullptr) {
    ESP_LOGW(kTag, "audio_play_tone_async called before a successful audio_init()");
    return ESP_ERR_INVALID_STATE;
  }
  if (!claim_playback_slot()) {
    ESP_LOGW(kTag, "audio_play_tone_async refused: playback already in progress");
    return ESP_ERR_INVALID_STATE;
  }
  auto request = std::make_unique<ToneRequest>(ToneRequest{frequency_hz, duration_ms});
  // PSRAM stack (kPlaybackTaskCaps), not internal RAM - see that constant's
  // own comment. Logged with the PSRAM numbers, not the internal-RAM ones,
  // on the rare chance this genuinely has none left: naming the wrong heap
  // would send whoever reads this log to the wrong culprit.
  if (xTaskCreateWithCaps(&tone_task, "audio_tone", kPlaybackTaskStackBytes,
                          request.get(), tskIDLE_PRIORITY + 1, nullptr,
                          kPlaybackTaskCaps) != pdPASS) {
    ESP_LOGE(kTag,
             "audio tone task creation failed: free PSRAM=%u largest "
             "PSRAM block=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    xSemaphoreGive(g_playback_busy);
    return ESP_ERR_NO_MEM;
  }
  (void)request.release();  // the task owns it now
  ESP_LOGI(kTag, "tone started: %d Hz, %d ms", frequency_hz, duration_ms);
  return ESP_OK;
}

esp_err_t audio_play_diagnostic_sweep_async(bool enable_amplifier) {
  if (g_codec_dev == nullptr) {
    ESP_LOGW(kTag,
             "audio_play_diagnostic_sweep_async called before a successful audio_init()");
    return ESP_ERR_INVALID_STATE;
  }
  if (!claim_playback_slot()) {
    ESP_LOGW(kTag, "audio_play_diagnostic_sweep_async refused: playback already in progress");
    return ESP_ERR_INVALID_STATE;
  }
  auto request = std::make_unique<SweepRequest>(SweepRequest{enable_amplifier});
  // See audio_play_tone_async()'s own comment: PSRAM stack, PSRAM numbers
  // if it fails.
  if (xTaskCreateWithCaps(&sweep_task, "audio_sweep", kPlaybackTaskStackBytes,
                          request.get(), tskIDLE_PRIORITY + 1, nullptr,
                          kPlaybackTaskCaps) != pdPASS) {
    ESP_LOGE(kTag,
             "audio sweep task creation failed: free PSRAM=%u largest "
             "PSRAM block=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    xSemaphoreGive(g_playback_busy);
    return ESP_ERR_NO_MEM;
  }
  (void)request.release();  // the task owns it now
  ESP_LOGI(kTag, "diagnostic sweep started (amplifier %s)",
           enable_amplifier ? "enabled" : "bypassed");
  return ESP_OK;
}

// Opens one PCM streaming session: claims the shared busy slot for the
// whole session (see g_playback_busy's own comment - not released until
// audio_stream_close() or the watchdog runs), then opens the codec/I2S
// channel at `sample_rate`. Always 16-bit, interleaved stereo - the one
// format every path in this module already uses (the I2S peripheral
// rejects an odd channel count outright), so there is no channels
// parameter to get wrong.
//
// No hardcoded whitelist of accepted rates: the ES8311 driver's own clock-
// coefficient table (es8311.c) is what actually decides, via
// esp_codec_dev_open() -> codec->set_fs() -> ESP_CODEC_DEV_NOT_SUPPORT on
// no match, and duplicating that table here by hand is exactly the kind of
// second copy that drifts. Confirmed present in that table, by reading it
// directly: 16000 Hz (this module's own tone rate) and 44100 Hz (AirPlay's
// rate) both have rows, so both are expected to succeed; 44100's row pairs
// with an 11.2896 MHz MCLK (256x), which is I2S_STD_CLK_DEFAULT_CONFIG's
// own default multiple - no mclk_multiple override needed.
//
// Amplifier and tray indicator are not touched here - see
// audio_stream_write() for why opening a stream that never receives a
// chunk must be silent and invisible.
esp_err_t audio_stream_open(int sample_rate) {
  if (g_codec_dev == nullptr) {
    ESP_LOGW(kTag, "audio_stream_open called before a successful audio_init()");
    return ESP_ERR_INVALID_STATE;
  }
  if (sample_rate <= 0) {
    ESP_LOGW(kTag, "audio_stream_open: invalid sample_rate %d", sample_rate);
    return ESP_ERR_INVALID_ARG;
  }
  if (!ensure_stream_primitives()) {
    ESP_LOGE(kTag, "audio stream: mutex/watchdog allocation failed");
    return ESP_ERR_NO_MEM;
  }
  if (!claim_playback_slot()) {
    ESP_LOGW(kTag, "audio_stream_open refused: playback already in progress");
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(g_stream_mutex, portMAX_DELAY);
  esp_codec_dev_sample_info_t sample_info{};
  sample_info.bits_per_sample = 16;
  sample_info.channel = kChannels;
  sample_info.sample_rate = static_cast<uint32_t>(sample_rate);
  // esp_codec_dev_open() is a silent no-op ("Input already open", see
  // esp_codec_dev.c) if the device is already open, rather than
  // reconfiguring it - claim_playback_slot() above is what guarantees this
  // is never reached while it already is, not this call. This is also why
  // switching rates (e.g. this stream's 44.1 kHz vs. the tone path's fixed
  // 16 kHz) needs the codec closed and reopened, never merely reconfigured
  // while open: reopening is the only path that actually calls back down
  // into the I2S data interface's set_fmt(), which is what reconfigures
  // the I2S peripheral's own clock generator (i2s_channel_reconfig_std_
  // clock(), confirmed by reading audio_codec_data_i2s.c) and the codec's
  // set_fs().
  const int result = esp_codec_dev_open(g_codec_dev, &sample_info);
  if (result != ESP_CODEC_DEV_OK) {
    ESP_LOGW(kTag,
             "audio stream: codec open failed at %d Hz: %d - see es8311.c's "
             "coeff_div[] table if this is neither 16000 nor 44100",
             sample_rate, result);
    xSemaphoreGive(g_stream_mutex);
    xSemaphoreGive(g_playback_busy);
    return ESP_FAIL;
  }
  // Re-applied on every open, same as audio_play_tone(): nothing here
  // assumes the codec remembers a volume from a previous, since-closed
  // session.
  esp_codec_dev_set_out_vol(g_codec_dev, g_volume_percent);
  g_stream_sample_rate = static_cast<uint32_t>(sample_rate);
  g_stream_amp_active = false;
  g_stream_open = true;
  arm_stream_watchdog();
  xSemaphoreGive(g_stream_mutex);

  ESP_LOGI(kTag, "audio stream opened: %d Hz", sample_rate);
  return ESP_OK;
}

// Writes one chunk of interleaved 16-bit stereo PCM at the rate given to
// audio_stream_open(). Blocking, exactly like esp_codec_dev_write() itself
// (and like the tone path's own writes) - the caller's own task is
// expected to own pacing this, the same way it owns decoding.
//
// The first successful call after audio_stream_open() - not open() itself -
// is what raises the amplifier and marks the tray indicator active, via
// the identical lead-in-silence-then-GPIO46-then-tray sequence
// write_tone_step() uses (kept as a separate, duplicated sequence rather
// than a shared helper - see this section's own opening comment for why).
// Both then stay set for the rest of the session; every later call in the
// same session only writes data and re-arms the watchdog.
//
// Every call re-arms the abandoned-stream watchdog: this is the "still
// alive" signal that timer is measuring the absence of.
esp_err_t audio_stream_write(const void* data, size_t length_bytes) {
  if (g_stream_mutex == nullptr) {
    ESP_LOGW(kTag, "audio_stream_write called with no stream ever opened");
    return ESP_ERR_INVALID_STATE;
  }
  xSemaphoreTake(g_stream_mutex, portMAX_DELAY);
  if (!g_stream_open) {
    xSemaphoreGive(g_stream_mutex);
    ESP_LOGW(kTag, "audio_stream_write called with no stream open");
    return ESP_ERR_INVALID_STATE;
  }
  arm_stream_watchdog();

  if (!g_stream_amp_active) {
    std::vector<int16_t> lead = make_silence(kSilenceLeadMs, g_stream_sample_rate);
    esp_codec_dev_write(g_codec_dev, lead.data(),
                        static_cast<int>(lead.size() * sizeof(int16_t)));
    ESP_LOGI(kTag, "tray indicator: requesting slot %d active=true",
             static_cast<int>(g_tray_indicator.slot));
    app_core::set_tray_indicator_active(g_tray_indicator, true);
    gpio_set_level(board::kAudioAmpEnable, 1);
    ESP_LOGI(kTag, "audio stream: GPIO46 (amp enable) reads %d after raising",
             gpio_get_level(board::kAudioAmpEnable));
    g_stream_amp_active = true;
  }

  const int result = esp_codec_dev_write(g_codec_dev, const_cast<void*>(data),
                                         static_cast<int>(length_bytes));
  const bool ok = (result == ESP_CODEC_DEV_OK);
  if (!ok) {
    // A write failure is a real driver problem, not a reason to leave the
    // stream open hoping the next call does better - "the amplifier ends
    // low on every exit path" applies to this one too, immediately, rather
    // than waiting kStreamWatchdogTimeoutMs for the watchdog to notice
    // nothing is working.
    ESP_LOGW(kTag,
             "audio stream: write failed (%d); closing the stream rather "
             "than leaving it half-broken",
             result);
    close_stream_locked();
  }
  xSemaphoreGive(g_stream_mutex);
  return ok ? ESP_OK : ESP_FAIL;
}

// Ends the session and guarantees the amplifier is left low - see
// close_stream_locked()'s own comment for the actual sequence. Always
// returns ESP_OK: safe (and a no-op) to call whether or not a stream is
// actually open, including after the watchdog has already force-closed it.
esp_err_t audio_stream_close() {
  if (g_stream_mutex == nullptr) return ESP_OK;  // never opened; nothing to do
  xSemaphoreTake(g_stream_mutex, portMAX_DELAY);
  close_stream_locked();
  xSemaphoreGive(g_stream_mutex);
  return ESP_OK;
}

void audio_set_volume(int percent) {
  g_volume_percent = std::clamp(percent, 0, 100);
  if (g_codec_dev != nullptr) {
    // Best-effort: the codec is normally closed between tones, in which
    // case this just updates the value applied at the next audio_play_tone().
    esp_codec_dev_set_out_vol(g_codec_dev, g_volume_percent);
  }
}

}  // namespace audio

#endif  // ESP_PLATFORM
