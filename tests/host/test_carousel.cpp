#include "test_support.hpp"

#include "carousel_controller.hpp"

using app_core::CarouselState;
using app_core::Transition;
using app_core::carousel::next;
using app_core::carousel::previous;
using app_core::carousel::tick;

HOST_TEST(home_advances_after_thirty_seconds) {
  const CarouselState initial{0, 1000, 0, false};
  const Transition transition = tick(initial, 31'000, 30);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(1));
  EXPECT_EQ(transition.state.page_started_ms, static_cast<uint64_t>(31'000));
}

HOST_TEST(data_page_advances_after_twelve_seconds) {
  const CarouselState initial{2, 1000, 0, false};
  const Transition transition = tick(initial, 13'000, 12);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(3));
}

HOST_TEST(key_next_enters_manual_pause) {
  const Transition transition = next(CarouselState{0, 1000, 0, false}, 2'000);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(1));
  EXPECT_TRUE(transition.state.manual_mode);
  EXPECT_EQ(transition.state.manual_until_ms, static_cast<uint64_t>(62'000));
}

HOST_TEST(boot_previous_wraps_to_last_page) {
  const Transition transition = previous(CarouselState{0, 1000, 0, false}, 2'000);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(4));
  EXPECT_TRUE(transition.state.manual_mode);
}

HOST_TEST(manual_pause_blocks_auto_advance_for_sixty_seconds) {
  const CarouselState manual{1, 2'000, 62'000, true};
  const Transition transition = tick(manual, 61'999, 12);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(1));
  EXPECT_TRUE(transition.state.manual_mode);
}

HOST_TEST(manual_timeout_returns_home_and_resumes_auto) {
  const CarouselState manual{3, 2'000, 62'000, true};
  const Transition transition = tick(manual, 62'000, 12);
  EXPECT_EQ(transition.state.index, static_cast<size_t>(0));
  EXPECT_EQ(transition.state.page_started_ms, static_cast<uint64_t>(62'000));
  EXPECT_TRUE(!transition.state.manual_mode);
}
