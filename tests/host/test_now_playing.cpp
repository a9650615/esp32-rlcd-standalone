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

}  // namespace

HOST_TEST(now_playing_layout_stays_inside_the_content_area) {
  const ui::Rect content =
      ui::content_bounds(ui::safe_canvas(), app_core::PageId::NowPlaying);

  for (const bool has_artwork : {false, true}) {
    const ui::NowPlayingLayout layout = ui::now_playing_layout(has_artwork);
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

HOST_TEST(now_playing_layouts_share_an_identical_transport_row) {
  const ui::NowPlayingLayout with = ui::now_playing_layout(true);
  const ui::NowPlayingLayout without = ui::now_playing_layout(false);
  EXPECT_EQ(with.state.y, without.state.y);
  EXPECT_EQ(with.state.x, without.state.x);
  EXPECT_EQ(with.time.y, without.time.y);
  EXPECT_EQ(with.progress_outline.y, without.progress_outline.y);
  EXPECT_EQ(with.progress_outline.height, without.progress_outline.height);
  EXPECT_EQ(with.progress_outline.width, without.progress_outline.width);
}

HOST_TEST(now_playing_artwork_is_square_and_absent_without_one) {
  const ui::NowPlayingLayout with = ui::now_playing_layout(true);
  EXPECT_EQ(with.artwork.width, ui::kNowPlayingArtworkSize);
  EXPECT_EQ(with.artwork.height, ui::kNowPlayingArtworkSize);
  EXPECT_EQ(ui::now_playing_layout(false).artwork.width, 0);
}

HOST_TEST(progress_fill_width_covers_its_whole_range) {
  const int full = ui::now_playing_layout(true).progress_outline.width - 4;
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
  const ui::Rect content =
      ui::content_bounds(ui::safe_canvas(), app_core::PageId::NowPlaying);
  const ui::VolumeOverlayLayout layout = ui::volume_overlay_layout();
  EXPECT_TRUE(inside(layout.label, content));
  EXPECT_TRUE(inside(layout.source, content));
  EXPECT_TRUE(inside(layout.value, content));
  EXPECT_TRUE(inside(layout.bar_outline, content));
  EXPECT_EQ(ui::volume_overlay_fill_width(0.0f), 0);
  EXPECT_EQ(ui::volume_overlay_fill_width(1.0f), layout.bar_outline.width - 6);
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
