#include "airplay.hpp"

#ifdef CONFIG_AIRPLAY_ENABLE

#include "esp_raop_receiver.h"

#include "audio.hpp"
#include "media_registry.hpp"
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

// --- Now-playing page: this module's own copy of what it has published so
// far. Kept here rather than read back out of the registry because RAOP
// delivers this in pieces - METADATA carries no progress, PROGRESS carries
// no title - and each event must update its own fields without blanking the
// others.
app_core::MediaSourceHandle g_media_source;
app_core::NowPlaying g_now_playing;

void publish() {
  // Volume is read at publish time rather than tracked: raop_get_volume()
  // returns the current level in SOFTWARE mode too (RAOP_EVENT_VOLUME is
  // HARDWARE-only), so there is nothing to subscribe to and nothing to keep
  // in sync.
  //
  // What it returns is not 0.0-1.0, though - it is handle->volume, set
  // verbatim from the RTSP SET_PARAMETER body's "volume: <dB>" field
  // (raop.c) and consumed the same raw way in audio_buffer.c's software
  // gain stage: AirPlay's wire format is dB, roughly -30.0 (quietest) to
  // 0.0 (loudest), with -144.0 as the protocol's explicit mute sentinel
  // (the exact threshold audio_buffer.c zeroes PCM at). Convert to the
  // 0.0-1.0 NowPlaying::volume expects, and leave the level untouched on
  // mute rather than reporting 0 - NowPlaying::muted is what says
  // "silenced"; the dB value that produced it is not recoverable once the
  // sender has overwritten handle->volume with -144.0, so the last known
  // level is the best available answer to "where the level was".
  if (g_handle != nullptr) {
    // Asked before the level is read, not inferred from the level itself.
    // Between SETUP and the sender's first SET_PARAMETER the library holds a
    // placeholder, and that placeholder is a perfectly ordinary dB value - it
    // was -20.0 when this was written, which this file's own conversion below
    // would have turned into a confident, wrong "33%" on the panel before any
    // sender had chosen anything. Testing the placeholder's value would also
    // make a sender that genuinely picks it read as unset. This flag is the
    // only thing that separates the two, and it re-arms on every SETUP.
    if (!raop_volume_is_known(g_handle)) {
      // Negative is NowPlaying::volume's own "nothing reported yet", which
      // the overlay already declines to open on.
      g_now_playing.volume = -1.0f;
      g_now_playing.muted = false;
    } else {
      const float vol_db = raop_get_volume(g_handle);
      if (vol_db <= -144.0f) {
        g_now_playing.muted = true;
      } else {
        constexpr float kMinDb = -30.0f;
        g_now_playing.muted = false;
        g_now_playing.volume =
            (std::clamp(vol_db, kMinDb, 0.0f) - kMinDb) / -kMinDb;
      }
    }
  }
  // A session ran with the tray icon lit, audio playing, and the page never
  // appearing - which means this call and the UI's own read of the registry
  // disagree, and nothing in either logs enough to say which side is wrong.
  // The handle's slot is here because an invalid one is silently ignored by
  // publish_now_playing() (it logs, but only under its own tag), and
  // session_open because that single bool is what the page's availability
  // and its seize both hang off.
  ESP_LOGI(kTag, "publish: slot=%d open=%d state=%d title='%s' vol=%.2f",
           static_cast<int>(g_media_source.slot),
           g_now_playing.session_open ? 1 : 0,
           static_cast<int>(g_now_playing.state), g_now_playing.title.c_str(),
           g_now_playing.volume);
  app_core::publish_now_playing(g_media_source, g_now_playing);
}

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
// real, already-wired dispatch sites, not stubs).
//
// Every other event (BUFFERING/PLAYING/STOPPED/PAUSED/METADATA/ARTWORK/
// PROGRESS/STALLED) is a sub-state within one still-open session. None of
// them touches the audio path - audio_stream_write() holds the amplifier
// continuously across chunks and only drops it at close, so reacting to
// play/pause there would add close-then-reopen churn for no benefit. They
// are handled below solely to keep the now-playing page current.
void handle_event(raop_event_t event, void *event_data,
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
      // Open the session before any title arrives: the page's own seize
      // state machine seizes the screen on the first tick session_open is
      // true, whatever the title is (an explicit was_open flag makes sure
      // an empty title still seizes) - waiting here for METADATA would just
      // delay that by however long the sender takes to send it.
      g_now_playing = app_core::NowPlaying{};
      g_now_playing.session_open = true;
      g_now_playing.state = app_core::MediaState::Buffering;
      // The protocol's own name, not a device name: RAOP exposes no API for
      // what the sender calls itself. A name invented here would be a claim
      // nothing backs.
      g_now_playing.source = "AIRPLAY";
      publish();
      break;
    }
    case RAOP_EVENT_DISCONNECTED:
      audio::audio_stream_close();
      ESP_LOGI(kTag, "tray indicator: requesting slot %d active=false",
               static_cast<int>(g_tray_indicator.slot));
      app_core::set_tray_indicator_active(g_tray_indicator, false);
      restore_wifi_power_save();
      g_now_playing = app_core::NowPlaying{};
      app_core::clear_media_session(g_media_source);
      break;
    case RAOP_EVENT_METADATA: {
      const auto *meta = static_cast<const raop_metadata_t *>(event_data);
      if (meta != nullptr) {
        g_now_playing.title = meta->title != nullptr ? meta->title : "";
        g_now_playing.subtitle = meta->artist != nullptr ? meta->artist : "";
        g_now_playing.detail = meta->album != nullptr ? meta->album : "";
      }
      // The first panel test showed a live progress bar and no title, which
      // three different faults produce identically: the sender never sent
      // metadata, the library parsed it but the event never reached here, or
      // it arrived with empty strings. raop.c already logs its own "received
      // metadata" line on the parse, so this line beside it tells the three
      // apart in one read of the log instead of a guess per attempt.
      ESP_LOGI(kTag, "metadata event: meta=%s title='%s' artist='%s' album='%s'",
               meta != nullptr ? "yes" : "NULL", g_now_playing.title.c_str(),
               g_now_playing.subtitle.c_str(), g_now_playing.detail.c_str());
      publish();
      break;
    }
    case RAOP_EVENT_PROGRESS: {
      const auto *progress = static_cast<const raop_progress_t *>(event_data);
      if (progress != nullptr) {
        g_now_playing.elapsed_ms = progress->current_ms;
        g_now_playing.total_ms = progress->total_ms;
      }
      publish();
      break;
    }
    case RAOP_EVENT_PLAYING:
      g_now_playing.state = app_core::MediaState::Playing;
      publish();
      break;
    case RAOP_EVENT_PAUSED:
      g_now_playing.state = app_core::MediaState::Paused;
      publish();
      break;
    case RAOP_EVENT_BUFFERING:
      g_now_playing.state = app_core::MediaState::Buffering;
      publish();
      break;
    case RAOP_EVENT_STALLED:
      g_now_playing.state = app_core::MediaState::Stalled;
      publish();
      break;
    case RAOP_EVENT_STOPPED:
      g_now_playing.state = app_core::MediaState::Stopped;
      g_now_playing.elapsed_ms = 0;
      publish();
      break;
    case RAOP_EVENT_VOLUME:
      // HARDWARE-mode-only (esp_raop_receiver.h) - this module always
      // configures RAOP_VOLUME_SOFTWARE (see airplay_init() below), so this
      // never fires. publish() reads raop_get_volume() directly instead;
      // see its own comment for why.
      break;
    case RAOP_EVENT_ARTWORK:
      // Not this task - see NowPlaying::artwork's own comment
      // (media_registry.hpp). Left default (null bits) so the page renders
      // its no-artwork layout.
      break;
    default:
      break;
  }
}

// esp_raop_receiver.h anchors ESP_ERR_RAOP_BASE at 0x7000; esp_http_client.h
// anchors ESP_ERR_HTTP_BASE at the same 0x7000 (confirmed by reading both
// headers side by side - this is not a guess). esp_err_to_name() walks a
// table of ranges registered by each component and returns the first name
// that matches a given number, so ESP_ERR_RAOP_NETWORK_FAILED (0x7003) and
// ESP_ERR_HTTP_WRITE_DATA (0x7000 + 3) are the same integer and
// esp_err_to_name() reports whichever one it finds - which is why a
// raop_init() failure has been logged as "ESP_ERR_HTTP_WRITE_DATA", a name
// from a component this module never calls. This does not affect any
// comparison against a specific named constant (== ESP_ERR_NOT_SUPPORTED
// still means exactly what it says) - only the printable name is wrong.
const char *raop_err_to_name(esp_err_t err) {
  switch (err) {
    case ESP_ERR_RAOP_NO_MEMORY: return "ESP_ERR_RAOP_NO_MEMORY";
    case ESP_ERR_RAOP_INVALID_CONFIG: return "ESP_ERR_RAOP_INVALID_CONFIG";
    case ESP_ERR_RAOP_NETWORK_FAILED: return "ESP_ERR_RAOP_NETWORK_FAILED";
    case ESP_ERR_RAOP_CODEC_FAILED: return "ESP_ERR_RAOP_CODEC_FAILED";
    case ESP_ERR_RAOP_MDNS_FAILED: return "ESP_ERR_RAOP_MDNS_FAILED";
    case ESP_ERR_RAOP_ALREADY_INIT: return "ESP_ERR_RAOP_ALREADY_INIT";
    case ESP_ERR_RAOP_NOT_INIT: return "ESP_ERR_RAOP_NOT_INIT";
    default: return esp_err_to_name(err);  // none of raop_init()'s other
                                            // possible codes collide
  }
}

}  // namespace

const char *airplay_err_to_name(esp_err_t err) { return raop_err_to_name(err); }

void airplay_register_tray() {
  // Registered unconditionally, before anything that could fail - same
  // reasoning as modules/audio/audio.cpp's own tray registration: the tray
  // reserves this module's slot the moment it is registered, not once
  // raop_init() actually succeeds, so the tray's layout does not shift
  // depending on it. See this function's declaration (airplay.hpp) for why
  // it is called separately from, and before, airplay_init().
  if (!g_tray_indicator.valid()) {
    build_icon_bitmap();
    g_tray_indicator = app_core::register_tray_indicator(
        {g_icon_bitmap, static_cast<uint8_t>(kIconWidth),
         static_cast<uint8_t>(kIconHeight)});
    if (!g_tray_indicator.valid()) {
      ESP_LOGW(kTag, "tray indicator registration failed: registry full");
    }
  }
}

esp_err_t airplay_init() {
  if (g_handle != nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  // Claimed here, not from airplay_register_tray(): unlike the tray cell,
  // whose width is fixed at the next full page rebuild after registration
  // (see that function's own comment), the now-playing page's availability
  // is polled fresh every carousel cycle from session_open
  // (page_registry.cpp's now_playing_available()), so nothing breaks if
  // this registers later than the tray does. It has no network dependency
  // either, but it belongs with the session machinery it feeds - claimed
  // unconditionally, before anything below that can fail, same reasoning
  // as the tray's own registration.
  if (!g_media_source.valid()) {
    g_media_source = app_core::register_media_source();
    if (!g_media_source.valid()) {
      ESP_LOGW(kTag, "media source registration failed: already taken");
    }
  }

#ifndef NDEBUG
  // The RTSP exchange is the only place a failed session says anything about
  // itself, and upstream logs it at ESP_LOGD - which means at this project's
  // default level it is not merely hidden but compiled out. Raising it for
  // this one tag is the difference between "the sender connected and left"
  // and knowing which method it got to.
  //
  // Only this tag: raising CONFIG_LOG_DEFAULT_LEVEL instead was tried, and
  // spi_master's per-transaction debug lines alone starved the portal's
  // socket until port 80 stopped answering - on a board whose firmware
  // upload shares that port.
  //
  // No effect unless CONFIG_LOG_MAXIMUM_LEVEL is DEBUG or higher, because
  // that is what decides whether ESP_LOGD exists in the binary at all.
  // Deliberately not asserted here: the useful build is the verbose one, and
  // a board that cannot say why a session failed is still better than one
  // that refuses to boot over a logging preference.
  esp_log_level_set("raop", ESP_LOG_DEBUG);
#endif

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
             raop_err_to_name(err),
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
