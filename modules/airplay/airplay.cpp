#include "airplay.hpp"

#ifdef CONFIG_AIRPLAY_ENABLE

#include "esp_raop_receiver.h"

#include "audio.hpp"
#include "tray_registry.hpp"

#include <algorithm>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_wifi.h>

namespace airplay {

namespace {

const char *kTag = "airplay";
raop_handle_t *g_handle = nullptr;

// --- Tray icon: registered directly with app_core's registry, exactly the
// pattern modules/audio/audio.cpp already established (register once, at
// init, before anything that could fail; toggle active/inactive around the
// session). This is the case the registry was designed for - a second
// module getting a tray icon needs zero changes to app_core or ui, only
// this module's own bitmap and its own calls to the same registry
// audio.cpp already uses.
//
// 16x12, 1 bit/pixel, row-major MSB-first, each row padded to a whole byte
// - the exact layout TrayIndicatorBitmap's own comment (tray_registry.hpp)
// documents, same size as modules/audio's icon so both read at a
// consistent scale in the tray.
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

// A small solid anchor at the bottom-centre - the device AirPlay is
// casting to - with two concentric arcs fanning upward from it: the
// conventional "broadcasting audio" glyph, bold and simple at this size.
// Built the same way modules/audio's own icon is (see its
// build_icon_bitmap()): a plain distance-from-centre band test per pixel,
// run once at startup rather than hand-encoded as a byte literal - this
// module supplies its icon as data, not code, the same contract audio's
// icon follows.
void build_icon_bitmap() {
  std::fill(g_icon_bitmap, g_icon_bitmap + sizeof(g_icon_bitmap), uint8_t{0});

  const int centre_x = kIconWidth / 2;
  const int anchor_y = kIconHeight - 1;
  constexpr int kAnchorHalfWidth = 2;
  for (int y = kIconHeight - 3; y < kIconHeight; ++y) {
    for (int x = centre_x - kAnchorHalfWidth; x <= centre_x + kAnchorHalfWidth;
        ++x) {
      set_icon_pixel(x, y);
    }
  }

  constexpr int kWaveCount = 2;
  constexpr int kStrokeWidth = 1;  // pixels; this bitmap is tiny, keep it thin
  const int max_radius = std::min(centre_x, anchor_y - 2);
  const int fan_top = anchor_y - 2;  // stop short of the anchor blob itself
  for (int i = 0; i < kWaveCount; ++i) {
    const int radius = max_radius * (i + 1) / kWaveCount;
    const int inner = std::max(0, radius - kStrokeWidth);
    for (int y = 0; y < fan_top; ++y) {
      for (int x = 0; x < kIconWidth; ++x) {
        const int dx = x - centre_x;
        const int dy = y - anchor_y;
        const int dist_sq = dx * dx + dy * dy;
        if (dist_sq <= radius * radius && dist_sq > inner * inner) {
          set_icon_pixel(x, y);
        }
      }
    }
  }
}

// --- Wi-Fi power save: off for the whole streaming session, restored to
// whatever it actually was once the session ends - not assumed back to
// ESP-IDF's own WIFI_PS_MIN_MODEM default, in case something else in this
// firmware ever sets a different mode. Sustained AirPlay audio over Wi-Fi
// with power save on glitches - there is no low-power "wait for a packet,
// sleep, wake" path through continuous RTP reception the way this board's
// other, bursty network activity (a portal request, an OTA check) can get
// away with.
wifi_ps_type_t g_saved_wifi_ps_mode = WIFI_PS_MIN_MODEM;

void disable_wifi_power_save() {
  if (esp_wifi_get_ps(&g_saved_wifi_ps_mode) != ESP_OK) {
    g_saved_wifi_ps_mode = WIFI_PS_MIN_MODEM;  // ESP-IDF's own default
  }
  const esp_err_t result = esp_wifi_set_ps(WIFI_PS_NONE);
  ESP_LOGI(kTag, "wifi power save disabled for streaming: %s",
           esp_err_to_name(result));
}

void restore_wifi_power_save() {
  const esp_err_t result = esp_wifi_set_ps(g_saved_wifi_ps_mode);
  ESP_LOGI(kTag, "wifi power save restored (mode %d): %s",
           static_cast<int>(g_saved_wifi_ps_mode), esp_err_to_name(result));
}

// --- RAOP callbacks ---------------------------------------------------

// Real audio, straight into modules/audio's streaming sink - no format
// adaptation needed: raop_audio_output_cb_t's own contract is already
// "16-bit stereo, 44.1kHz" (esp_raop_receiver.h), exactly what
// audio_stream_open(44100) (see handle_event() below) expects.
//
// Does not track "is a stream open" itself: audio::audio_stream_write()
// already refuses cleanly - logged, ESP_ERR_INVALID_STATE, no crash, no
// corrupted state - if called with none open. That can only happen here
// if a frame arrives outside the CONNECTED/DISCONNECTED bracket below,
// which would itself mean the RAOP session's own event ordering broke -
// worth that log line existing on its own account, not worth this module
// duplicating the open/closed state to guard against a case its one
// caller (raop_core.c) is not supposed to produce.
void feed_audio(const uint8_t *data, size_t len, void * /*user_ctx*/) {
  const esp_err_t result = audio::audio_stream_write(data, len);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "audio_stream_write failed: %s", esp_err_to_name(result));
  }
}

// Session boundaries, from the RAOP receiver's own event callback rather
// than inferred from feed_audio() going quiet - CONNECTED fires once, at
// RTSP SETUP, before any audio can arrive; DISCONNECTED fires once, at
// RTSP TEARDOWN, after which audio_buffer_deinit() means no more audio
// ever will (confirmed by reading raop_core.c's internal_cmd_cb - both are
// real, already-wired dispatch sites, not stubs). Every other event
// (BUFFERING/PLAYING/STOPPED/PAUSED/VOLUME/METADATA/ARTWORK/PROGRESS/
// STALLED) is a sub-state within one still-open session and is
// deliberately not handled here: audio_stream_write() already holds the
// amplifier continuously across chunks and only drops it at close, so
// reacting to every play/pause within a session would only add close-then-
// reopen churn for no benefit.
void handle_event(raop_event_t event, void * /*event_data*/,
                  void * /*user_ctx*/) {
  switch (event) {
    case RAOP_EVENT_CONNECTED: {
      disable_wifi_power_save();
      ESP_LOGI(kTag, "tray indicator: requesting slot %d active=true",
               static_cast<int>(g_tray_indicator.slot));
      app_core::set_tray_indicator_active(g_tray_indicator, true);
      // AirPlay 1 streams at exactly one rate; see
      // modules/audio/README.md's streaming section for why this needs
      // the codec closed and reopened rather than merely reconfigured,
      // and for what happens if a notification tone is requested while
      // this session is open (refused - the same busy guard
      // audio_play_tone_async() already checks, not anything new here).
      const esp_err_t result = audio::audio_stream_open(44100);
      if (result != ESP_OK) {
        ESP_LOGW(kTag,
                 "audio_stream_open failed: %s - this session's audio has "
                 "nowhere to go",
                 esp_err_to_name(result));
      }
      break;
    }
    case RAOP_EVENT_DISCONNECTED:
      audio::audio_stream_close();
      ESP_LOGI(kTag, "tray indicator: requesting slot %d active=false",
               static_cast<int>(g_tray_indicator.slot));
      app_core::set_tray_indicator_active(g_tray_indicator, false);
      restore_wifi_power_save();
      break;
    default:
      break;
  }
}

}  // namespace

esp_err_t airplay_init() {
  if (g_handle != nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  // Registered unconditionally, before anything below that could fail -
  // same reasoning as modules/audio/audio.cpp's own tray registration: the
  // tray reserves this module's slot regardless of whether raop_init()
  // actually succeeds, so the tray's layout does not shift depending on
  // it.
  if (!g_tray_indicator.valid()) {
    build_icon_bitmap();
    g_tray_indicator = app_core::register_tray_indicator(
        {g_icon_bitmap, static_cast<uint8_t>(kIconWidth),
         static_cast<uint8_t>(kIconHeight)});
    if (!g_tray_indicator.valid()) {
      ESP_LOGW(kTag, "tray indicator registration failed: registry full");
    }
  }

  raop_config_t config = {};
  config.audio_output_cb = feed_audio;
  config.event_cb = handle_event;
  config.mdns_mode = RAOP_MDNS_MANAGED;
  config.volume_mode = RAOP_VOLUME_SOFTWARE;

  esp_err_t err = raop_init(&config, &g_handle);
  if (err != ESP_OK) {
    // raop_ctx_s (esp-raop-receiver/src/raop.c) is one heap_caps_malloc(...,
    // MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) block - it embeds the RTSP and
    // "search remote" task stacks as member arrays, so it needs one
    // contiguous ~27KB internal-DRAM block to succeed, not just that much
    // free in total. A failure here is indistinguishable from mDNS/network
    // setup failing - raop_core.c's raop_init() returns the same
    // ESP_ERR_RAOP_NETWORK_FAILED whether IP resolution, raop_create()'s
    // socket bind, or this allocation is what actually failed - so these
    // figures are logged as context for that possibility, not asserted as
    // the cause. Same fields as app_main.cpp's boot-time
    // "startup diagnostics" log, largest_free_block (not just free) because
    // this allocation needs one contiguous block.
    ESP_LOGE(kTag,
             "raop_init failed: %s (internal RAM free=%u "
             "largest_free_block=%u)",
             esp_err_to_name(err),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    g_handle = nullptr;
  }
  return err;
}

esp_err_t airplay_deinit() {
  if (g_handle == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t err = raop_deinit(g_handle);
  g_handle = nullptr;
  return err;
}

}  // namespace airplay

#endif  // CONFIG_AIRPLAY_ENABLE
