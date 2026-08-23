#include "media_registry.hpp"

#include "test_support.hpp"

HOST_TEST(media_registry_starts_with_no_session) {
  app_core::reset_media_registry_for_test();
  EXPECT_TRUE(!app_core::now_playing().session_open);
}

HOST_TEST(media_registry_accepts_one_source_and_rejects_a_second) {
  app_core::reset_media_registry_for_test();
  const app_core::MediaSourceHandle first = app_core::register_media_source();
  const app_core::MediaSourceHandle second = app_core::register_media_source();
  EXPECT_TRUE(first.valid());
  EXPECT_TRUE(!second.valid());
}

HOST_TEST(media_registry_publishes_and_reads_back) {
  app_core::reset_media_registry_for_test();
  const app_core::MediaSourceHandle handle = app_core::register_media_source();

  app_core::NowPlaying state;
  state.session_open = true;
  state.state = app_core::MediaState::Playing;
  state.source = "AIRPLAY";
  state.title = "Midnight City";
  state.subtitle = "M83";
  state.detail = "Hurry Up, We're Dreaming";
  state.elapsed_ms = 102'000;
  state.total_ms = 238'000;
  state.volume = 0.6f;
  app_core::publish_now_playing(handle, state);

  const app_core::NowPlaying read = app_core::now_playing();
  EXPECT_TRUE(read.session_open);
  EXPECT_TRUE(read.title == "Midnight City");
  EXPECT_EQ(read.elapsed_ms, 102'000u);
  EXPECT_TRUE(read.state == app_core::MediaState::Playing);
}

HOST_TEST(media_registry_ignores_publishes_from_an_invalid_handle) {
  app_core::reset_media_registry_for_test();
  app_core::NowPlaying state;
  state.session_open = true;
  state.title = "should not appear";
  app_core::publish_now_playing(app_core::MediaSourceHandle{}, state);
  EXPECT_TRUE(!app_core::now_playing().session_open);
}

HOST_TEST(media_registry_clear_closes_the_session) {
  app_core::reset_media_registry_for_test();
  const app_core::MediaSourceHandle handle = app_core::register_media_source();
  app_core::NowPlaying state;
  state.session_open = true;
  state.title = "Midnight City";
  app_core::publish_now_playing(handle, state);
  app_core::clear_media_session(handle);

  const app_core::NowPlaying read = app_core::now_playing();
  EXPECT_TRUE(!read.session_open);
  EXPECT_TRUE(read.title.empty());
}
