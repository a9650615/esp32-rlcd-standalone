#define UI_THEME_GEOMETRY_ONLY
#include "app_snapshot.hpp"
#include "media_registry.hpp"
#include "now_playing_controller.hpp"
#include "page_registry.hpp"
#include "ui_data.hpp"

#include "test_support.hpp"

#include <algorithm>

namespace {

// Opens a session with enough fields set that a layout can be built from it.
app_core::MediaSourceHandle open_test_session() {
  app_core::reset_media_registry_for_test();
  const app_core::MediaSourceHandle handle = app_core::register_media_source();
  app_core::NowPlaying state;
  state.session_open = true;
  state.state = app_core::MediaState::Playing;
  state.source = "AIRPLAY";
  state.title = "Midnight City";
  state.subtitle = "M83";
  state.elapsed_ms = 102'000;
  state.total_ms = 238'000;
  state.volume = 0.6f;
  app_core::publish_now_playing(handle, state);
  return handle;
}

bool cycle_contains(const std::vector<app_core::PageId>& pages,
                    app_core::PageId page) {
  return std::find(pages.begin(), pages.end(), page) != pages.end();
}

}  // namespace

HOST_TEST(now_playing_page_is_absent_with_no_session) {
  app_core::reset_media_registry_for_test();
  app_core::PageRegistry registry;
  registry.begin_cycle(app_core::make_mock_snapshot(
      app_core::DemoScenario::TaiwanSession));
  EXPECT_TRUE(!cycle_contains(registry.page_ids(), app_core::PageId::NowPlaying));
}

HOST_TEST(now_playing_page_joins_the_cycle_while_a_session_is_open) {
  open_test_session();
  app_core::PageRegistry registry;
  registry.begin_cycle(app_core::make_mock_snapshot(
      app_core::DemoScenario::TaiwanSession));
  EXPECT_TRUE(cycle_contains(registry.page_ids(), app_core::PageId::NowPlaying));
  app_core::reset_media_registry_for_test();
}

HOST_TEST(now_playing_page_carries_the_tray_and_the_dots) {
  EXPECT_TRUE(ui::page_shows_tray(app_core::PageId::NowPlaying));
  EXPECT_TRUE(ui::page_shows_dots(app_core::PageId::NowPlaying));
}

namespace {

bool inside(const ui::Rect& inner, const ui::Rect& outer) {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}

// The real content rect for this page, in the same absolute safe_canvas()
// frame every other page's own layout_fits static_assert already checks
// against (see setup_layout_fits and friends in ui_data.hpp). Passing this
// straight to now_playing_layout()/volume_overlay_layout() is a legitimate
// use, not a stand-in for the renderer's actual (zero-offset, relative-to-
// the-page-root) frame - see now_playing_layout_shape_is_invariant_to_its_
// origin below for the test that exercises the renderer's own frame.
ui::Rect now_playing_content() {
  return ui::content_bounds(ui::safe_canvas(), app_core::PageId::NowPlaying);
}

}  // namespace

HOST_TEST(now_playing_layout_stays_inside_the_content_area) {
  const ui::Rect content = now_playing_content();

  for (const bool has_artwork : {false, true}) {
    const ui::NowPlayingLayout layout =
        ui::now_playing_layout(content, has_artwork);
    EXPECT_TRUE(inside(layout.title, content));
    EXPECT_TRUE(inside(layout.subtitle, content));
    EXPECT_TRUE(inside(layout.detail, content));
    EXPECT_TRUE(inside(layout.source, content));
    EXPECT_TRUE(inside(layout.state, content));
    EXPECT_TRUE(inside(layout.time, content));
    EXPECT_TRUE(inside(layout.progress_outline, content));
    if (has_artwork) EXPECT_TRUE(inside(layout.artwork, content));
  }
}

// The bug this project actually shipped: now_playing_layout() used to build
// every rect from absolute canvas literals, so it produced the exact same
// answer regardless of what origin its caller was working in - correct only
// by coincidence at content.x == content.y == 0, and silently wrong once
// render_page() started passing the real (already-offset) content rect.
// now_playing_layout_stays_inside_the_content_area above could not catch
// that: it only ever exercised one fixed absolute box, and the
// double-counted offset it was checking for still landed inside a content
// box large enough to absorb it, on top of the safe canvas itself hiding
// six of those eight pixels. This test proves the property the bug actually
// broke - that the layout is pure arithmetic on the rect it is handed, the
// same convention setup_layout/market_chart_rect/weather_forecast_rect
// already follow - by shifting the origin by an arbitrary (nonzero,
// including negative) amount and checking every rect moves by exactly that
// amount: same width, same height, same position relative to every other
// rect in the layout, just translated.
HOST_TEST(now_playing_layout_shape_is_invariant_to_its_origin) {
  constexpr int dx = 37;
  constexpr int dy = -19;
  const ui::Rect origin_a{0, 0, 388, 300};
  const ui::Rect origin_b{origin_a.x + dx, origin_a.y + dy, 388, 300};

  for (const bool has_artwork : {false, true}) {
    const ui::NowPlayingLayout a =
        ui::now_playing_layout(origin_a, has_artwork);
    const ui::NowPlayingLayout b =
        ui::now_playing_layout(origin_b, has_artwork);
    const ui::Rect* rects_a[] = {&a.artwork, &a.source,  &a.title,
                                 &a.subtitle, &a.detail, &a.state,
                                 &a.time,     &a.progress_outline};
    const ui::Rect* rects_b[] = {&b.artwork, &b.source,  &b.title,
                                 &b.subtitle, &b.detail, &b.state,
                                 &b.time,     &b.progress_outline};
    for (std::size_t i = 0; i < sizeof(rects_a) / sizeof(rects_a[0]); ++i) {
      EXPECT_EQ(rects_a[i]->width, rects_b[i]->width);
      EXPECT_EQ(rects_a[i]->height, rects_b[i]->height);
      // The absent-artwork rect is the zero rect at both origins - nothing
      // to translate, and 0 + dx would wrongly fail this loop for it.
      if (rects_a[i]->width == 0 && rects_a[i]->height == 0) continue;
      EXPECT_EQ(rects_a[i]->x + dx, rects_b[i]->x);
      EXPECT_EQ(rects_a[i]->y + dy, rects_b[i]->y);
    }
  }
}

HOST_TEST(now_playing_layouts_share_an_identical_transport_row) {
  const ui::Rect content = now_playing_content();
  const ui::NowPlayingLayout with = ui::now_playing_layout(content, true);
  const ui::NowPlayingLayout without = ui::now_playing_layout(content, false);
  EXPECT_EQ(with.state.y, without.state.y);
  EXPECT_EQ(with.state.x, without.state.x);
  EXPECT_EQ(with.time.y, without.time.y);
  EXPECT_EQ(with.progress_outline.y, without.progress_outline.y);
  EXPECT_EQ(with.progress_outline.height, without.progress_outline.height);
  EXPECT_EQ(with.progress_outline.width, without.progress_outline.width);
}

HOST_TEST(now_playing_artwork_is_square_and_absent_without_one) {
  const ui::Rect content = now_playing_content();
  const ui::NowPlayingLayout with = ui::now_playing_layout(content, true);
  EXPECT_EQ(with.artwork.width, ui::kNowPlayingArtworkSize);
  EXPECT_EQ(with.artwork.height, ui::kNowPlayingArtworkSize);
  EXPECT_EQ(ui::now_playing_layout(content, false).artwork.width, 0);
}

HOST_TEST(artwork_fits_slot_accepts_exactly_the_reserved_size) {
  EXPECT_TRUE(ui::now_playing_artwork_fits_slot(ui::kNowPlayingArtworkSize,
                                                ui::kNowPlayingArtworkSize));
}

HOST_TEST(artwork_fits_slot_accepts_smaller_than_the_reserved_size) {
  EXPECT_TRUE(ui::now_playing_artwork_fits_slot(1, 1));
  EXPECT_TRUE(ui::now_playing_artwork_fits_slot(
      ui::kNowPlayingArtworkSize - 1, ui::kNowPlayingArtworkSize - 1));
}

HOST_TEST(artwork_fits_slot_rejects_wider_than_the_reserved_size) {
  EXPECT_TRUE(!ui::now_playing_artwork_fits_slot(
      ui::kNowPlayingArtworkSize + 1, ui::kNowPlayingArtworkSize));
}

HOST_TEST(artwork_fits_slot_rejects_taller_than_the_reserved_size) {
  EXPECT_TRUE(!ui::now_playing_artwork_fits_slot(
      ui::kNowPlayingArtworkSize, ui::kNowPlayingArtworkSize + 1));
}

HOST_TEST(artwork_fits_slot_rejects_zero_dimensions) {
  EXPECT_TRUE(!ui::now_playing_artwork_fits_slot(0, ui::kNowPlayingArtworkSize));
  EXPECT_TRUE(!ui::now_playing_artwork_fits_slot(ui::kNowPlayingArtworkSize, 0));
  EXPECT_TRUE(!ui::now_playing_artwork_fits_slot(0, 0));
}

HOST_TEST(progress_fill_width_covers_its_whole_range) {
  // Derived independently, from an actual layout built at a real content
  // rect, rather than trusting now_playing_progress_fill_width()'s own
  // internal span - this is what "still agree with the rects" means: the
  // outline width the fill is scaled against must match the outline width
  // now_playing_layout() actually hands the renderer, not just whatever
  // the fill function happens to compute for itself.
  const ui::NowPlayingLayout layout =
      ui::now_playing_layout(now_playing_content(), true);
  const int full = layout.progress_outline.width - 4;
  EXPECT_EQ(ui::now_playing_progress_fill_width(0, 238'000), 0);
  EXPECT_EQ(ui::now_playing_progress_fill_width(238'000, 238'000), full);
  // A stream that overruns its declared length must not draw past the outline.
  EXPECT_EQ(ui::now_playing_progress_fill_width(400'000, 238'000), full);
  // Unknown length (a live stream): no bar at all.
  EXPECT_EQ(ui::now_playing_progress_fill_width(102'000, 0), 0);
}

HOST_TEST(track_time_formats_as_minutes_and_seconds) {
  EXPECT_TRUE(ui::format_track_time(0) == "0:00");
  EXPECT_TRUE(ui::format_track_time(102'000) == "1:42");
  EXPECT_TRUE(ui::format_track_time(238'000) == "3:58");
  EXPECT_TRUE(ui::format_track_time(3'661'000) == "61:01");
  // Truncates, never rounds up: 1:59.9 is still 1:59.
  EXPECT_TRUE(ui::format_track_time(119'900) == "1:59");
}

HOST_TEST(volume_text_distinguishes_mute_from_silence_and_from_absence) {
  EXPECT_TRUE(ui::volume_percent_text(0.6f, false) == "60");
  EXPECT_TRUE(ui::volume_percent_text(0.0f, false) == "0");
  EXPECT_TRUE(ui::volume_percent_text(1.0f, false) == "100");
  EXPECT_TRUE(ui::volume_percent_text(0.6f, true) == "MUTE");
  EXPECT_TRUE(ui::volume_percent_text(-1.0f, false).empty());
}

HOST_TEST(media_state_labels_are_printable_ascii_and_distinct) {
  const app_core::MediaState states[] = {
      app_core::MediaState::Playing, app_core::MediaState::Paused,
      app_core::MediaState::Buffering, app_core::MediaState::Stalled,
      app_core::MediaState::Stopped};
  for (const app_core::MediaState state : states) {
    const char* text = ui::media_state_label(state);
    EXPECT_TRUE(text != nullptr && text[0] != '\0');
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
      EXPECT_TRUE(*cursor >= 0x20 && *cursor <= 0x7e);
    }
  }
  EXPECT_TRUE(ui::media_state_label(app_core::MediaState::Playing) !=
              ui::media_state_label(app_core::MediaState::Paused));
}

HOST_TEST(volume_overlay_fits_the_content_area) {
  const ui::Rect content = now_playing_content();
  const ui::VolumeOverlayLayout layout = ui::volume_overlay_layout(content);
  EXPECT_TRUE(inside(layout.label, content));
  EXPECT_TRUE(inside(layout.source, content));
  EXPECT_TRUE(inside(layout.value, content));
  EXPECT_TRUE(inside(layout.bar_outline, content));
  EXPECT_EQ(ui::volume_overlay_fill_width(0.0f), 0);
  // Agrees with the fill function's own (independently derived) span, the
  // same cross-check progress_fill_width_covers_its_whole_range does above.
  EXPECT_EQ(ui::volume_overlay_fill_width(1.0f), layout.bar_outline.width - 6);
}

// Same bug, same proof, on the overlay: shifting the content rect's origin
// must shift every overlay rect by exactly the same amount and change
// nothing else. See now_playing_layout_shape_is_invariant_to_its_origin
// above for why a fixed absolute box cannot catch this.
HOST_TEST(volume_overlay_layout_shape_is_invariant_to_its_origin) {
  constexpr int dx = 37;
  constexpr int dy = -19;
  const ui::Rect origin_a{0, 0, 388, 300};
  const ui::Rect origin_b{origin_a.x + dx, origin_a.y + dy, 388, 300};

  const ui::VolumeOverlayLayout a = ui::volume_overlay_layout(origin_a);
  const ui::VolumeOverlayLayout b = ui::volume_overlay_layout(origin_b);
  const ui::Rect* rects_a[] = {&a.label, &a.source, &a.value, &a.bar_outline};
  const ui::Rect* rects_b[] = {&b.label, &b.source, &b.value, &b.bar_outline};
  for (std::size_t i = 0; i < sizeof(rects_a) / sizeof(rects_a[0]); ++i) {
    EXPECT_EQ(rects_a[i]->width, rects_b[i]->width);
    EXPECT_EQ(rects_a[i]->height, rects_b[i]->height);
    EXPECT_EQ(rects_a[i]->x + dx, rects_b[i]->x);
    EXPECT_EQ(rects_a[i]->y + dy, rects_b[i]->y);
  }
}

HOST_TEST(seize_takes_the_screen_when_a_session_opens) {
  app_core::SeizeState state;
  EXPECT_TRUE(!app_core::seize_tick(state, false, "", 1'000).owns_screen);

  const app_core::SeizeState opened =
      app_core::seize_tick(state, true, "Midnight City", 1'000);
  EXPECT_TRUE(opened.owns_screen);
}

HOST_TEST(seize_releases_after_the_hold_and_stays_released) {
  app_core::SeizeState state =
      app_core::seize_tick(app_core::SeizeState{}, true, "Midnight City", 1'000);
  const uint64_t hold_ms = app_core::kNowPlayingSeizeSeconds * 1000;

  state = app_core::seize_tick(state, true, "Midnight City", 1'000 + hold_ms - 1);
  EXPECT_TRUE(state.owns_screen);

  state = app_core::seize_tick(state, true, "Midnight City", 1'000 + hold_ms);
  EXPECT_TRUE(!state.owns_screen);

  state = app_core::seize_tick(state, true, "Midnight City", 9'999'999);
  EXPECT_TRUE(!state.owns_screen);
}

HOST_TEST(seize_restarts_on_a_new_title_but_not_on_a_republished_one) {
  const uint64_t hold_ms = app_core::kNowPlayingSeizeSeconds * 1000;
  app_core::SeizeState state =
      app_core::seize_tick(app_core::SeizeState{}, true, "Midnight City", 1'000);
  state = app_core::seize_tick(state, true, "Midnight City", 1'000 + hold_ms);
  EXPECT_TRUE(!state.owns_screen);

  // Same title again: no reason to grab the screen back.
  state = app_core::seize_tick(state, true, "Midnight City", 1'000 + hold_ms + 5);
  EXPECT_TRUE(!state.owns_screen);

  // A different one is a new track.
  state = app_core::seize_tick(state, true, "Reunion", 1'000 + hold_ms + 10);
  EXPECT_TRUE(state.owns_screen);
}

HOST_TEST(seize_lets_go_the_moment_the_session_closes) {
  app_core::SeizeState state =
      app_core::seize_tick(app_core::SeizeState{}, true, "Midnight City", 1'000);
  EXPECT_TRUE(state.owns_screen);
  state = app_core::seize_tick(state, false, "", 1'500);
  EXPECT_TRUE(!state.owns_screen);
}

HOST_TEST(volume_overlay_opens_on_a_change_and_closes_on_a_timer) {
  app_core::VolumeOverlayState state;
  // First observation only records the level; there is nothing to compare it
  // against yet, so restoring a volume at connect time must not flash the
  // overlay.
  state = app_core::volume_overlay_tick(state, 0.6f, true, 1'000);
  EXPECT_TRUE(!state.visible);

  state = app_core::volume_overlay_tick(state, 0.7f, true, 2'000);
  EXPECT_TRUE(state.visible);

  state = app_core::volume_overlay_tick(state, 0.7f, true,
                                        2'000 + app_core::kVolumeOverlayMs - 1);
  EXPECT_TRUE(state.visible);

  state = app_core::volume_overlay_tick(state, 0.7f, true,
                                        2'000 + app_core::kVolumeOverlayMs);
  EXPECT_TRUE(!state.visible);
}

HOST_TEST(volume_overlay_stays_shut_when_the_page_is_not_on_screen) {
  app_core::VolumeOverlayState state =
      app_core::volume_overlay_tick(app_core::VolumeOverlayState{}, 0.6f, false,
                                    1'000);
  state = app_core::volume_overlay_tick(state, 0.9f, false, 2'000);
  EXPECT_TRUE(!state.visible);

  // Leaving the page closes an overlay that was already up.
  app_core::VolumeOverlayState open =
      app_core::volume_overlay_tick(app_core::VolumeOverlayState{}, 0.6f, true,
                                    1'000);
  open = app_core::volume_overlay_tick(open, 0.9f, true, 2'000);
  EXPECT_TRUE(open.visible);
  open = app_core::volume_overlay_tick(open, 0.9f, false, 2'100);
  EXPECT_TRUE(!open.visible);
}

HOST_TEST(volume_overlay_ignores_a_source_with_no_level_to_report) {
  app_core::VolumeOverlayState state;
  state = app_core::volume_overlay_tick(state, -1.0f, true, 1'000);
  state = app_core::volume_overlay_tick(state, -1.0f, true, 2'000);
  EXPECT_TRUE(!state.visible);

  // The first real level after that is still only a baseline, not a change.
  state = app_core::volume_overlay_tick(state, 0.4f, true, 3'000);
  EXPECT_TRUE(!state.visible);
  state = app_core::volume_overlay_tick(state, 0.5f, true, 4'000);
  EXPECT_TRUE(state.visible);
}

HOST_TEST(seize_takes_the_screen_when_a_session_opens_with_no_title_yet) {
  // A session can open before any metadata has arrived: session_open true,
  // no title published yet. Comparing titles alone ("" != "") would never
  // seize in that case, and a source that never publishes a title at all
  // would never seize, ever.
  app_core::SeizeState state;
  state = app_core::seize_tick(state, true, "", 1'000);
  EXPECT_TRUE(state.owns_screen);
}

HOST_TEST(seize_ignores_a_clock_that_moves_backward) {
  app_core::SeizeState state = app_core::seize_tick(
      app_core::SeizeState{}, true, "Midnight City", 10'000);
  EXPECT_TRUE(state.owns_screen);

  // now_ms goes backward - an unguarded subtraction would underflow and
  // release the hold immediately.
  state = app_core::seize_tick(state, true, "Midnight City", 1'000);
  EXPECT_TRUE(state.owns_screen);
}

HOST_TEST(volume_overlay_ignores_a_clock_that_moves_backward) {
  app_core::VolumeOverlayState state;
  state = app_core::volume_overlay_tick(state, 0.6f, true, 10'000);
  state = app_core::volume_overlay_tick(state, 0.7f, true, 20'000);
  EXPECT_TRUE(state.visible);

  // now_ms goes backward - an unguarded subtraction would underflow and
  // close the overlay immediately.
  state = app_core::volume_overlay_tick(state, 0.7f, true, 1'000);
  EXPECT_TRUE(state.visible);
}

HOST_TEST(volume_overlay_closes_on_timeout_even_while_volume_is_unknown) {
  app_core::VolumeOverlayState state;
  state = app_core::volume_overlay_tick(state, 0.6f, true, 1'000);
  state = app_core::volume_overlay_tick(state, 0.7f, true, 2'000);
  EXPECT_TRUE(state.visible);

  // The source stops reporting a level while the overlay is up. The timeout
  // must still fire; a negative reading must not pin the overlay open.
  state = app_core::volume_overlay_tick(state, -1.0f, true,
                                        2'000 + app_core::kVolumeOverlayMs);
  EXPECT_TRUE(!state.visible);
}

// --- ElapsedTracker: the derivation that stands in for AirPlay 1's absent
// continuous progress updates (see that struct's own comment,
// now_playing_controller.hpp, for the real capture that proved the sender
// only reports at track start and on a seek). airplay.cpp is not itself
// host-tested - it only compiles under CONFIG_AIRPLAY_ENABLE/ESP_PLATFORM -
// but every bit of arithmetic worth getting wrong here lives in these four
// pure functions, which is exactly why they were pulled out into
// now_playing_controller.hpp rather than left inline in airplay.cpp's
// handle_event().

HOST_TEST(elapsed_stays_at_the_report_while_not_advancing) {
  // A fresh session (or one that is buffering/paused) must not move at all,
  // however much wall-clock time passes - ElapsedTracker{} defaults to
  // advancing == false.
  const app_core::ElapsedTracker state;
  EXPECT_EQ(app_core::elapsed_now(state, 0), 0u);
  EXPECT_EQ(app_core::elapsed_now(state, 60'000), 0u);
}

HOST_TEST(elapsed_counts_up_on_the_local_clock_while_playing) {
  app_core::ElapsedTracker state;
  state = app_core::elapsed_on_report(state, 10'000, 238'000, 1'000);
  state = app_core::elapsed_on_resume(state, 1'000);
  EXPECT_EQ(app_core::elapsed_now(state, 1'000), 10'000u);
  // Five real seconds pass with no new report from the sender - exactly the
  // gap AirPlay 1 leaves for the rest of a track.
  EXPECT_EQ(app_core::elapsed_now(state, 6'000), 15'000u);
}

HOST_TEST(elapsed_reanchors_on_a_real_report_discarding_the_derived_value) {
  // The sender is authoritative any time it actually speaks - a seek
  // backward (or forward) must win over whatever had been derived since the
  // last report, not be averaged or ignored.
  app_core::ElapsedTracker state;
  state = app_core::elapsed_on_report(state, 10'000, 238'000, 1'000);
  state = app_core::elapsed_on_resume(state, 1'000);

  // A seek lands at 90 s, reported 6 s (local time) after the first report.
  state = app_core::elapsed_on_report(state, 90'000, 238'000, 6'000);
  EXPECT_EQ(app_core::elapsed_now(state, 6'000), 90'000u);
  EXPECT_EQ(app_core::elapsed_now(state, 9'000), 93'000u);
}

HOST_TEST(elapsed_freezes_on_pause_at_the_derived_position_not_the_last_report) {
  // The bug this guards against: freezing at reported_ms (still 0, from
  // track start) instead of at the position actually reached by the time
  // the pause happened.
  app_core::ElapsedTracker state;
  state = app_core::elapsed_on_report(state, 0, 238'000, 1'000);
  state = app_core::elapsed_on_resume(state, 1'000);
  // 50 s of playback, then paused.
  state = app_core::elapsed_on_freeze(state, 51'000);
  EXPECT_EQ(app_core::elapsed_now(state, 51'000), 50'000u);
  // Frozen: further wall-clock time must not move it.
  EXPECT_EQ(app_core::elapsed_now(state, 500'000), 50'000u);
}

HOST_TEST(elapsed_resumes_from_the_frozen_position_not_ahead_by_the_pause) {
  // A pause held for 5 real minutes must not be added to the displayed
  // position on resume - elapsed_on_resume() anchors at *now*, not at
  // received_at_ms + how long the pause lasted.
  app_core::ElapsedTracker state;
  state = app_core::elapsed_on_report(state, 0, 238'000, 1'000);
  state = app_core::elapsed_on_resume(state, 1'000);
  state = app_core::elapsed_on_freeze(state, 51'000);  // paused at 50 s
  EXPECT_EQ(app_core::elapsed_now(state, 51'000), 50'000u);

  const uint64_t resumed_at_ms = 51'000 + 5 * 60'000;  // 5 minutes later
  state = app_core::elapsed_on_resume(state, resumed_at_ms);
  EXPECT_EQ(app_core::elapsed_now(state, resumed_at_ms), 50'000u);
  EXPECT_EQ(app_core::elapsed_now(state, resumed_at_ms + 2'000), 52'000u);
}

HOST_TEST(elapsed_never_exceeds_a_known_total) {
  // A stream that overruns its declared length (the exact case
  // now_playing_progress_fill_width() already guards, above) must not push
  // the derived clock past total_ms either.
  app_core::ElapsedTracker state;
  state = app_core::elapsed_on_report(state, 230'000, 238'000, 1'000);
  state = app_core::elapsed_on_resume(state, 1'000);
  EXPECT_EQ(app_core::elapsed_now(state, 1'000 + 20'000), 238'000u);
}

HOST_TEST(elapsed_is_never_clamped_when_the_length_is_unknown) {
  // total_ms == 0 means a live stream, not a zero-length track - see
  // NowPlaying::total_ms's own comment. The derived clock must keep
  // counting up past any particular number, not get pinned at 0.
  app_core::ElapsedTracker state;
  state = app_core::elapsed_on_report(state, 5'000, 0, 1'000);
  state = app_core::elapsed_on_resume(state, 1'000);
  EXPECT_EQ(app_core::elapsed_now(state, 1'000 + 600'000), 605'000u);
}

HOST_TEST(elapsed_ignores_a_clock_that_moves_backward) {
  // Same guard seize_tick()/volume_overlay_tick() already carry, for the
  // same underflow reason: an unguarded now_ms - received_at_ms would wrap
  // to a number in the billions of milliseconds and the progress bar would
  // jump to full (or past total_ms, if not for the separate clamp above).
  app_core::ElapsedTracker state;
  state = app_core::elapsed_on_report(state, 10'000, 238'000, 10'000);
  state = app_core::elapsed_on_resume(state, 10'000);
  EXPECT_EQ(app_core::elapsed_now(state, 1'000), 10'000u);
}
