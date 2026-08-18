#pragma once

#ifdef ESP_PLATFORM

#include <esp_err.h>

// AirPlay 1 (RAOP) receiver - see modules/airplay/Kconfig.projbuild
// (CONFIG_AIRPLAY_ENABLE) and modules/airplay/README.md. Vendors
// esp-raop-receiver (the RAOP protocol: RTSP, RTP, mDNS advertisement, DMAP
// metadata) plus Apple's own open-source ALAC decoder in place of upstream's
// prebuilt libalac.a - see modules/airplay/UPSTREAM.md for the provenance of
// both.
//
// airplay_init() starts a real RAOP receiver - RTSP/RTP sockets, mDNS
// advertisement, ALAC decoding of real audio - and feeds the decoded PCM
// into modules/audio's streaming sink (audio::audio_stream_open()/_write()/
// _close()) for the session's lifetime, using the RAOP receiver's own
// connected/disconnected events as the session boundary (see airplay.cpp's
// handle_event()). It disables Wi-Fi power save for the session's duration.
// airplay_register_tray() registers this module's own tray indicator,
// active only while a session is open - split out from airplay_init()
// because it has no network dependency and airplay_init() does (see its own
// comment). Every function exists unconditionally so a caller never writes
// an #ifdef of its own, same contract as modules/audio/include/audio.hpp.
namespace airplay {

#ifdef CONFIG_AIRPLAY_ENABLE

// Reserves this module's tray slot. Call once, early - before Wi-Fi exists,
// let alone has an address - same timing as modules/audio's own tray
// registration. Unlike airplay_init() below, this has no network dependency
// at all, which is exactly why it is a separate call: app_core's tray
// layout reserves a cell for a slot the moment it is *registered*, not once
// it goes active (see components/ui/render_shared.cpp's render_tray()), and
// that reservation only updates at the next full page rebuild - so if this
// ran after the first rebuild instead of before it, this module's cell
// would come up zero-width and could stay that way indefinitely. Calling it
// here, at the same point audio_init() calls its own equivalent, keeps both
// modules' slots reserved before the first frame is ever drawn.
void airplay_register_tray();

// Starts mDNS advertisement and the RAOP receiver (RTSP listener, RTP/ALAC
// decode path). From here on, a connecting AirPlay client's decoded audio
// reaches modules/audio's streaming sink for real - see the namespace
// comment above.
//
// Must be called only once the Wi-Fi station has a real, DHCP-assigned IP
// address - not merely once esp_netif/the event loop exist.
// raop_init() (esp-raop-receiver/src/raop_core.c) resolves that address
// itself via esp_netif_get_ip_info() on WIFI_STA_DEF and fails immediately
// if it is still 0.0.0.0; it does not wait or retry. See
// main/app_main.cpp's airplay_startup_task, which is created after
// wifi_provision::start() and waits for the address for exactly this
// reason, the same pattern net_log_startup_task and three monitor tasks in
// that file already use.
//
// A failure here must never be treated as fatal by the caller, same
// reasoning as audio::audio_init(): this board's primary job is the
// display.
//
// Idempotent in the sense that a second call while already running returns
// ESP_ERR_INVALID_STATE rather than starting a second instance - it does not
// retry or reconfigure an existing one.
esp_err_t airplay_init();

// Stops the RAOP receiver and mDNS advertisement airplay_init() started.
// ESP_ERR_INVALID_STATE if airplay_init() was never called or already failed.
esp_err_t airplay_deinit();

// Renders an esp_err_t airplay_init()/airplay_deinit() can return as text.
// Do not hand either function's result to esp_err_to_name() directly: this
// module's ESP_ERR_RAOP_* codes (esp_raop_receiver.h) and esp_http_client's
// own error codes (esp_http_client.h) both start their numeric range at the
// same 0x7000 base, so esp_err_to_name() resolves an unrelated
// esp_http_client name for any of them - see airplay.cpp's definition for
// the specific collision this was written against. Falls back to
// esp_err_to_name() for every other code these functions can return
// (ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, ESP_ERR_INVALID_STATE,
// ESP_ERR_NOT_SUPPORTED), none of which collide.
const char* airplay_err_to_name(esp_err_t err);

#else  // !CONFIG_AIRPLAY_ENABLE

// Inline no-ops, not a runtime check: with the option off, airplay.cpp and
// every vendored RAOP/ALAC source file are not even compiled (see
// CMakeLists.txt), so there is no RTSP/RTP socket, no mDNS dependency, and
// no ALAC decoder in this build at all.
inline void airplay_register_tray() {}
inline esp_err_t airplay_init() { return ESP_ERR_NOT_SUPPORTED; }
inline esp_err_t airplay_deinit() { return ESP_ERR_NOT_SUPPORTED; }
// No collision to guard against here - ESP_ERR_NOT_SUPPORTED is nowhere
// near the 0x7000 range - but declared unconditionally anyway so a caller
// never needs an #ifdef of its own to format either function's result.
inline const char* airplay_err_to_name(esp_err_t err) {
  return esp_err_to_name(err);
}

#endif  // CONFIG_AIRPLAY_ENABLE

}  // namespace airplay

#endif  // ESP_PLATFORM
