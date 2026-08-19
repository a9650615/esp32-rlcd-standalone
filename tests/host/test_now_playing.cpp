#define UI_THEME_GEOMETRY_ONLY
#include "app_snapshot.hpp"
#include "media_registry.hpp"
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
