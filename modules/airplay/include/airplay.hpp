#pragma once

#ifdef ESP_PLATFORM

#include <esp_err.h>

// AirPlay 1 (RAOP) receiver skeleton - see modules/airplay/Kconfig.projbuild
// (CONFIG_AIRPLAY_ENABLE) and modules/airplay/README.md. Vendors
// esp-raop-receiver (the RAOP protocol: RTSP, RTP, mDNS advertisement, DMAP
// metadata) plus Apple's own open-source ALAC decoder in place of upstream's
// prebuilt libalac.a - see modules/airplay/UPSTREAM.md for the provenance of
// both.
//
// This is a skeleton, not a feature yet: airplay_init() below starts a real
// RAOP receiver - RTSP/RTP sockets, mDNS advertisement, ALAC decoding of real
// audio - but the decoded PCM currently goes nowhere (see airplay.cpp). No
// caller wires this to modules/audio, the tray, or main/app_main.cpp yet;
// that integration is a deliberately separate pass. Every function still
// exists unconditionally so a future caller never writes an #ifdef of its
// own, same contract as modules/audio/include/audio.hpp.
namespace airplay {

#ifdef CONFIG_AIRPLAY_ENABLE

// Starts mDNS advertisement and the RAOP receiver (RTSP listener, RTP/ALAC
// decode path). Decoded PCM is discarded, not played - see the namespace
// comment above and airplay.cpp's discard_audio(). This proves the receiver
// itself runs; it does not yet make sound.
//
// A failure here must never be treated as fatal by a future caller, same
// reasoning as audio::audio_init(): this board's primary job is the display.
//
// Idempotent in the sense that a second call while already running returns
// ESP_ERR_INVALID_STATE rather than starting a second instance - it does not
// retry or reconfigure an existing one.
esp_err_t airplay_init();

// Stops the RAOP receiver and mDNS advertisement airplay_init() started.
// ESP_ERR_INVALID_STATE if airplay_init() was never called or already failed.
esp_err_t airplay_deinit();

#else  // !CONFIG_AIRPLAY_ENABLE

// Inline no-ops, not a runtime check: with the option off, airplay.cpp and
// every vendored RAOP/ALAC source file are not even compiled (see
// CMakeLists.txt), so there is no RTSP/RTP socket, no mDNS dependency, and
// no ALAC decoder in this build at all.
inline esp_err_t airplay_init() { return ESP_ERR_NOT_SUPPORTED; }
inline esp_err_t airplay_deinit() { return ESP_ERR_NOT_SUPPORTED; }

#endif  // CONFIG_AIRPLAY_ENABLE

}  // namespace airplay

#endif  // ESP_PLATFORM
