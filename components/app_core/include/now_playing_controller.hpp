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

// Derives NowPlaying::elapsed_ms between the sender's own progress reports.
// AirPlay 1 does not push progress continuously - a sender sends
// SET_PARAMETER progress once at track start and again on a seek, and
// nothing else (see modules/airplay/airplay.cpp's RAOP_EVENT_PROGRESS
// comment for the capture that proved it: three progress messages in a
// 35-second real session, all in the first two seconds). Left as the raw
// last-reported value, the page's progress bar and elapsed clock simply
// stop moving for the rest of the track. This ticks the displayed value
// forward on the device's own clock between reports, and snaps back to
// whatever the sender says whenever it actually speaks - the sender is
// authoritative any time it has an opinion; this only fills the silence.
struct ElapsedTracker {
  // The sender's last-known position and the track's declared length -
  // "last-known" rather than "last-reported" because elapsed_on_freeze()
  // below also writes here, snapshotting the derived value at the moment
  // playback stops advancing so a later resume continues from the right
  // place (see that function's own comment).
  uint32_t reported_ms = 0;
  // 0 means unknown length (a live stream), matching NowPlaying::total_ms's
  // own convention - carried here too so elapsed_now() below can apply the
  // same never-clamp rule without the caller having to pass it in separately.
  uint32_t total_ms = 0;
  // The local (device) clock reading at which reported_ms became current -
  // never the sender's clock, which this device cannot read continuously,
  // only sample at whatever moments the sender chooses to report.
  uint64_t received_at_ms = 0;
  // Whether the derived value should currently be advancing. False across
  // paused, buffering, stalled and stopped alike - all four mean "freeze
  // whatever is on screen", and true only while genuinely playing.
  bool advancing = false;
};

// The value to publish right now: reported_ms, plus however long it has
// been advancing since it was last anchored, clamped to total_ms once a
// length is known (total_ms == 0 stays unclamped - a live stream has no
// ceiling to hit). Also the guard against a backward-moving clock: the same
// hazard seize_tick()/volume_overlay_tick() already guard against for
// exactly the same reason (an unguarded now_ms - received_at_ms would
// underflow to a number in the billions of milliseconds and the progress
// bar would jump to full).
inline uint32_t elapsed_now(const ElapsedTracker& state, uint64_t now_ms) {
  if (!state.advancing || now_ms < state.received_at_ms) {
    return state.reported_ms;
  }
  const uint64_t derived = static_cast<uint64_t>(state.reported_ms) +
                            (now_ms - state.received_at_ms);
  if (state.total_ms != 0 && derived > state.total_ms) {
    return state.total_ms;
  }
  // Only reachable past ~49.7 days of one track advancing continuously with
  // total_ms == 0 (unknown length) - not a real session, but clamped rather
  // than left to truncate silently into a small, wrong number.
  return derived > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(derived);
}

// A real progress report arrived (RAOP_EVENT_PROGRESS) - re-anchor at what
// the sender actually said, discarding whatever had been derived since the
// last one. Does not touch `advancing`: a report can arrive at any time
// (notably a seek while paused), and whether the displayed value should be
// moving is entirely the play/pause state's business, not the report's.
inline ElapsedTracker elapsed_on_report(ElapsedTracker state,
                                        uint32_t reported_ms,
                                        uint32_t total_ms, uint64_t now_ms) {
  state.reported_ms = reported_ms;
  state.total_ms = total_ms;
  state.received_at_ms = now_ms;
  return state;
}

// Playback stopped advancing (paused, buffering, stalled, or stopped).
// Snapshots elapsed_now()'s current value into reported_ms before clearing
// `advancing` - without this, a pause after 50 s of playback would freeze
// the display back at whatever the last SET_PARAMETER said (often 0, from
// track start) instead of at 50 s, because reported_ms was never updated
// while `advancing` was doing the counting-up on its own.
inline ElapsedTracker elapsed_on_freeze(ElapsedTracker state,
                                        uint64_t now_ms) {
  state.reported_ms = elapsed_now(state, now_ms);
  state.received_at_ms = now_ms;
  state.advancing = false;
  return state;
}

// Playback started or resumed (RAOP_EVENT_PLAYING). Re-anchors at *now*,
// not at whatever local time the last report or the last freeze happened -
// a session paused for five minutes must resume counting from where
// elapsed_on_freeze() left reported_ms, not five minutes further along, and
// anchoring at now_ms rather than trying to account for the pause's
// duration is what makes that automatic.
inline ElapsedTracker elapsed_on_resume(ElapsedTracker state,
                                        uint64_t now_ms) {
  state.received_at_ms = now_ms;
  state.advancing = true;
  return state;
}

}  // namespace app_core
