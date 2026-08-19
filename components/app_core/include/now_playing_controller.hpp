#pragma once

#include <cstdint>
#include <string>

// Two small state machines for the now-playing page, kept out of ui_app.cpp
// for the same reason carousel_controller.hpp next door is: they are pure
// functions over plain structs, so a host test can drive a whole session in a
// loop with no clock, no tasks, and no LVGL.
namespace app_core {

// How long the page holds the screen when a session opens, before rotation
// resumes. A starting value to tune against the real panel, not a derived
// number - it lives here, beside the other timing constants, precisely so
// that tuning it is a one-line change and not an archaeology exercise.
inline constexpr uint32_t kNowPlayingSeizeSeconds = 60;

// How long the volume overlay stays up after the last change. Deliberately
// the same 2 s as ui::kNavigationOverlayDurationMs: both are "something just
// happened, show it briefly", and two different durations for that would read
// as an inconsistency rather than a distinction.
inline constexpr uint32_t kVolumeOverlayMs = 2'000;

struct SeizeState {
  bool owns_screen = false;
  uint64_t seized_ms = 0;
  // What was playing when the screen was last seized. Compared by value: a
  // republished identical title is the same track, and only a genuinely
  // different one is a new one worth interrupting the carousel for.
  std::string title;
  // Whether this session has already been seen open on some earlier tick.
  // Needed because a session can open before any metadata has arrived -
  // session_open true, title still "" - and SeizeState{}'s own title also
  // defaults to "", so comparing titles alone would read "" != "" as false
  // and never seize. was_open makes the first tick of an open session seize
  // unconditionally, whatever the title is at that point.
  bool was_open = false;
};

// `now_ms` is the same monotonic millisecond clock the carousel already runs
// on. Returns the next state; the caller keeps it.
inline SeizeState seize_tick(SeizeState state, bool session_open,
                             const std::string& title, uint64_t now_ms) {
  if (!session_open) return SeizeState{};
  if (!state.was_open || title != state.title) {
    // Covers the first tick of a session (was_open false, so this fires
    // regardless of title) and every later track change (title differs from
    // the one last seized on) with the same branch.
    return SeizeState{true, now_ms, title, true};
  }
  // Guard against a backward-moving clock the same way carousel_controller
  // does (see its tick()): an unguarded now_ms - state.seized_ms would
  // underflow to near UINT64_MAX and release the hold immediately. A clock
  // that has gone backward has not yet reached the timeout, so it must not
  // release either.
  if (state.owns_screen && now_ms >= state.seized_ms &&
      now_ms - state.seized_ms >= kNowPlayingSeizeSeconds * 1000) {
    state.owns_screen = false;
  }
  return state;
}

struct VolumeOverlayState {
  bool visible = false;
  uint64_t shown_ms = 0;
  // Negative means nothing has been observed yet. The first real reading only
  // establishes a baseline - without that, restoring a volume when a session
  // connects would flash the overlay for a change nobody made.
  float last_volume = -1.0f;
};

// `page_on_screen` is whether the now-playing page is the page currently
// rendered - not whether a session is open. The overlay annotates this page;
// popping it over the weather would be a notification, which is a different
// feature nobody asked for.
inline VolumeOverlayState volume_overlay_tick(VolumeOverlayState state,
                                              float volume,
                                              bool page_on_screen,
                                              uint64_t now_ms) {
  if (!page_on_screen) {
    state.visible = false;
    return state;
  }
  // A negative reading means the source has nothing to report right now; it
  // must not open the overlay or move the baseline, but it also must not
  // pin an already-open overlay past its timeout, so it falls straight
  // through to the close check below instead of returning early.
  if (volume >= 0.0f) {
    if (state.last_volume < 0.0f) {
      state.last_volume = volume;  // baseline only
      return state;
    }
    if (volume != state.last_volume) {
      // Exact equality is safe today only because volume is a direct
      // passthrough from the publishing module with no local arithmetic on
      // it. A future source that derives it (averaging, scaling) would need
      // an epsilon compare instead.
      state.last_volume = volume;
      state.visible = true;
      state.shown_ms = now_ms;
      return state;
    }
  }
  // Guard against a backward-moving clock the same way seize_tick() and
  // carousel_controller's tick() do: an unguarded subtraction would
  // underflow and close the overlay immediately. A clock that has gone
  // backward has not yet reached the timeout, so it must not close either.
  if (state.visible && now_ms >= state.shown_ms &&
      now_ms - state.shown_ms >= kVolumeOverlayMs) {
    state.visible = false;
  }
  return state;
}

}  // namespace app_core
