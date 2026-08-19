#pragma once

#include <cstdint>
#include <string>

// One media session, registered by a module and rendered by ui::. Shaped
// deliberately like tray_registry.hpp, for the same reason: modules/README.md
// rule 4 forbids core from knowing which module is playing something, so core
// reserves the slot and the module fills it. Nothing here names a protocol -
// AirPlay is the first source to use it, and a second one (Bluetooth A2DP, a
// local player) reuses the page rather than adding another.
//
// Not part of AppSnapshot on purpose: that struct is republished wholesale by
// several tasks on their own cadences, which is exactly how the field-
// overwrite bug documented on AppSnapshot::battery_runtime happened. A
// registry the publisher writes directly has no shared struct to be
// clobbered through.
namespace app_core {

enum class MediaState { Idle, Buffering, Playing, Paused, Stalled, Stopped };

// A 1-bit, tightly-packed, row-major MSB-first bitmap - byte for byte the
// layout TrayIndicatorBitmap already documents, so nothing new about it
// needs explaining. Owned by the publisher, which must keep it alive until
// it publishes a different one or clears the session. Core blits the bytes
// and never interprets them.
struct MediaArtwork {
  const uint8_t* bits = nullptr;
  uint16_t width = 0;
  uint16_t height = 0;
};

struct NowPlaying {
  bool session_open = false;
  MediaState state = MediaState::Idle;
  // Whatever the source calls itself: a device name if it knows one, its own
  // protocol name if not. Core supplies no default and does not care which.
  std::string source;
  std::string title;
  std::string subtitle;  // artist
  std::string detail;    // album
  uint32_t elapsed_ms = 0;
  // 0 means unknown length (a live stream), not zero-length. The page draws
  // elapsed time and no progress bar in that case.
  uint32_t total_ms = 0;
  // Negative means the source has no volume to report yet, which is distinct
  // from silence: the overlay does not open on it. 0.0-1.0 otherwise.
  float volume = -1.0f;
  // Its own flag rather than volume 0.0, because a source that signals mute
  // separately from level is telling us two different things: turned all the
  // way down, and silenced with the level left where it was. The page says
  // MUTE for one and 0% for the other. How a given protocol encodes that is
  // the publishing module's problem, not this file's.
  bool muted = false;
  MediaArtwork artwork;
};

// Returned by register_media_source(). An invalid handle (see valid()) means
// a source was already registered; callers must check rather than assume.
struct MediaSourceHandle {
  int8_t slot = -1;
  constexpr bool valid() const { return slot >= 0; }
};

// One slot, not an array: two simultaneous sources is not a real situation on
// this board. A second registration returns an invalid handle, the same way a
// full tray registry does.
MediaSourceHandle register_media_source();

// Safe to call from any task. A no-op for an invalid handle, logged loudly on
// target - an unwired handle reaching here is a caller bug and must not look
// like a successful publish in the log.
void publish_now_playing(MediaSourceHandle handle, const NowPlaying& state);

// Ends the session: everything back to defaults, so no stale title survives
// into the next one. A no-op for an invalid handle.
void clear_media_session(MediaSourceHandle handle);

// What the UI reads, every tick. A default-constructed NowPlaying
// (session_open == false) when nothing is playing.
NowPlaying now_playing();

// Test-only, same purpose as reset_tray_registry_for_test(): host tests
// register a source repeatedly across independent cases and must not leak
// state between them.
void reset_media_registry_for_test();

}  // namespace app_core
