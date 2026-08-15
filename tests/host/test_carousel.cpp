#include "test_support.hpp"

#include "app_snapshot.hpp"
#include "carousel_controller.hpp"
#include "page_registry.hpp"

using app_core::AppSnapshot;
using app_core::CarouselState;
using app_core::DemoScenario;
using app_core::PageRegistry;
using app_core::Transition;
using app_core::kPageCount;
using app_core::make_mock_snapshot;
using app_core::carousel::next;
using app_core::carousel::previous;
using app_core::carousel::tick;

HOST_TEST(home_advances_after_thirty_seconds) {
  const CarouselState initial{0, 0, 0, false};
  EXPECT_EQ(tick(initial, 29'999, 30, kPageCount).state.index,
            static_cast<size_t>(0));
  const Transition transition = tick(initial, 30'000, 30, kPageCount);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(1));
  EXPECT_EQ(transition.state.page_started_ms, static_cast<uint64_t>(30'000));
}

HOST_TEST(data_page_advances_after_twelve_seconds) {
  const CarouselState initial{2, 0, 0, false};
  EXPECT_EQ(tick(initial, 11'999, 12, kPageCount).state.index,
            static_cast<size_t>(2));
  const Transition transition = tick(initial, 12'000, 12, kPageCount);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(3));
}

HOST_TEST(backward_time_never_advances) {
  const CarouselState initial{2, 10'000, 0, false};
  const Transition transition = tick(initial, 9'999, 12, kPageCount);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(2));
  EXPECT_EQ(transition.state.page_started_ms, static_cast<uint64_t>(10'000));
}

HOST_TEST(key_next_enters_manual_pause) {
  const Transition transition =
      next(CarouselState{0, 1000, 0, false}, 2'000, kPageCount);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(1));
  EXPECT_TRUE(transition.state.manual_mode);
  EXPECT_EQ(transition.state.manual_until_ms, static_cast<uint64_t>(62'000));
}

HOST_TEST(boot_previous_wraps_to_last_page) {
  const Transition transition =
      previous(CarouselState{0, 1000, 0, false}, 2'000, kPageCount);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(4));
  EXPECT_TRUE(transition.state.manual_mode);
}

HOST_TEST(omitted_page_count_wraps_previous_with_four_pages) {
  AppSnapshot snapshot = make_mock_snapshot(DemoScenario::TaiwanSession);
  snapshot.availability.weather = false;
  PageRegistry registry;
  registry.begin_cycle(snapshot);
  EXPECT_EQ(registry.size(), static_cast<std::size_t>(4));
  const Transition transition =
      previous(CarouselState{0, 1'000, 0, false}, 2'000, registry.size());
  EXPECT_EQ(transition.state.index, static_cast<size_t>(3));
}

HOST_TEST(empty_registry_count_is_a_safe_noop) {
  const CarouselState state{0, 1'000, 0, false};
  const Transition next_transition = next(state, 2'000, 0);
  const Transition previous_transition = previous(state, 2'000, 0);
  const Transition tick_transition = tick(state, 100'000, 12, 0);
  EXPECT_EQ(next_transition.state.index, static_cast<size_t>(0));
  EXPECT_EQ(previous_transition.state.index, static_cast<size_t>(0));
  EXPECT_EQ(tick_transition.state.index, static_cast<size_t>(0));
  EXPECT_TRUE(!next_transition.page_changed);
  EXPECT_TRUE(!previous_transition.page_changed);
  EXPECT_TRUE(!tick_transition.page_changed);
}

HOST_TEST(manual_pause_blocks_auto_advance_for_sixty_seconds) {
  const CarouselState manual{1, 2'000, 62'000, true};
  const Transition transition = tick(manual, 61'999, 12, kPageCount);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(1));
  EXPECT_TRUE(transition.state.manual_mode);
}

HOST_TEST(manual_timeout_returns_home_and_resumes_auto) {
  const CarouselState manual{3, 2'000, 62'000, true};
  const Transition transition = tick(manual, 62'000, 12, kPageCount);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(0));
  EXPECT_EQ(transition.state.page_started_ms, static_cast<uint64_t>(62'000));
  EXPECT_TRUE(!transition.state.manual_mode);
}
