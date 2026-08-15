#include "board_buttons.hpp"
#include "test_support.hpp"

#include <cstddef>
#include <initializer_list>

namespace {

using board::ButtonEvent;
using board::ButtonFilter;

void expect_events(const ButtonFilter::Events& actual,
                   std::initializer_list<ButtonEvent> expected) {
  EXPECT_EQ(actual.count, expected.size());
  std::size_t index = 0;
  for (const ButtonEvent event : expected) {
    EXPECT_EQ(actual.events[index], event);
    ++index;
  }
}

}  // namespace

HOST_TEST(button_bounce_emits_one_previous_on_stable_key_release) {
  ButtonFilter filter;

  // The active-low KEY input bounces before settling pressed and released.
  expect_events(filter.sample(false, false), {});
  expect_events(filter.sample(true, false), {});
  expect_events(filter.sample(false, false), {});
  expect_events(filter.sample(true, false), {});
  expect_events(filter.sample(true, false), {});
  expect_events(filter.sample(true, false), {});
  expect_events(filter.sample(false, false), {});
  expect_events(filter.sample(true, false), {});
  expect_events(filter.sample(false, false), {});
  expect_events(filter.sample(false, false), {});
  expect_events(filter.sample(false, false), {ButtonEvent::Previous});
  expect_events(filter.sample(false, false), {});
}

HOST_TEST(button_hold_emits_no_repeat_and_one_event_after_release) {
  ButtonFilter filter;

  for (int sample = 0; sample < 3; ++sample) {
    expect_events(filter.sample(true, false), {});
  }
  for (int sample = 0; sample < 20; ++sample) {
    expect_events(filter.sample(true, false), {});
  }
  for (int sample = 0; sample < 2; ++sample) {
    expect_events(filter.sample(false, false), {});
  }
  expect_events(filter.sample(false, false), {ButtonEvent::Previous});
  expect_events(filter.sample(false, false), {});
}

HOST_TEST(button_boot_release_emits_next) {
  ButtonFilter filter;

  for (int sample = 0; sample < 3; ++sample) {
    expect_events(filter.sample(false, true), {});
  }
  for (int sample = 0; sample < 2; ++sample) {
    expect_events(filter.sample(false, false), {});
  }
  expect_events(filter.sample(false, false), {ButtonEvent::Next});
  expect_events(filter.sample(false, false), {});
}

HOST_TEST(button_simultaneous_release_reports_both_once_in_pin_order) {
  ButtonFilter filter;

  for (int sample = 0; sample < 3; ++sample) {
    expect_events(filter.sample(true, true), {});
  }
  for (int sample = 0; sample < 2; ++sample) {
    expect_events(filter.sample(false, false), {});
  }
  expect_events(filter.sample(false, false),
                {ButtonEvent::Previous, ButtonEvent::Next});
  expect_events(filter.sample(false, false), {});
}

HOST_TEST(button_key_short_release_still_emits_previous) {
  ButtonFilter filter;

  for (int sample = 0; sample < 3; ++sample) {
    expect_events(filter.sample(true, false), {});
  }
  for (int sample = 0; sample < 10; ++sample) {
    expect_events(filter.sample(true, false), {});
  }
  for (int sample = 0; sample < 2; ++sample) {
    expect_events(filter.sample(false, false), {});
  }
  expect_events(filter.sample(false, false), {ButtonEvent::Previous});
  expect_events(filter.sample(false, false), {});
}

HOST_TEST(button_key_long_press_fires_once_then_suppresses_release_previous) {
  ButtonFilter filter;

  // Stabilize KEY pressed; the held-sample count starts on this sample.
  for (int sample = 0; sample < 3; ++sample) {
    expect_events(filter.sample(true, false), {});
  }
  // kLongPressSamples - 2 further held samples still fall short of firing.
  for (int sample = 0; sample < static_cast<int>(ButtonFilter::kLongPressSamples) - 2; ++sample) {
    expect_events(filter.sample(true, false), {});
  }
  // This sample is the kLongPressSamples-th held sample: fires exactly once.
  expect_events(filter.sample(true, false), {ButtonEvent::EnterSetup});

  // Continuing to hold emits nothing further.
  for (int sample = 0; sample < 50; ++sample) {
    expect_events(filter.sample(true, false), {});
  }

  // Releasing after a long press does not emit Previous.
  for (int sample = 0; sample < 2; ++sample) {
    expect_events(filter.sample(false, false), {});
  }
  expect_events(filter.sample(false, false), {});
  expect_events(filter.sample(false, false), {});
}

HOST_TEST(button_key_release_one_sample_before_long_press_threshold_still_emits_previous) {
  ButtonFilter filter;

  for (int sample = 0; sample < 3; ++sample) {
    expect_events(filter.sample(true, false), {});
  }
  // Leaves the held-sample count one short of the threshold once the
  // release debounce below consumes its two pending-release samples.
  for (int sample = 0; sample < static_cast<int>(ButtonFilter::kLongPressSamples) - 4; ++sample) {
    expect_events(filter.sample(true, false), {});
  }
  for (int sample = 0; sample < 2; ++sample) {
    expect_events(filter.sample(false, false), {});
  }
  expect_events(filter.sample(false, false), {ButtonEvent::Previous});
  expect_events(filter.sample(false, false), {});
}

HOST_TEST(button_boot_long_press_opens_the_menu_and_suppresses_its_release) {
  ButtonFilter filter;

  // BOOT long press was previously unused, on the theory that GPIO0 being the
  // download strap made it unsafe. The strap is sampled at reset, not while
  // running, so this is the settings gesture now - mirroring KEY exactly, or
  // the same hold would mean different things on different buttons.
  for (int sample = 0; sample < 3; ++sample) {
    expect_events(filter.sample(false, true), {});
  }
  for (int sample = 0; sample < static_cast<int>(ButtonFilter::kLongPressSamples) - 2; ++sample) {
    expect_events(filter.sample(false, true), {});
  }
  expect_events(filter.sample(false, true), {ButtonEvent::OpenMenu});

  for (int sample = 0; sample < 50; ++sample) {
    expect_events(filter.sample(false, true), {});
  }

  // Releasing after the long press must not also page forward.
  for (int sample = 0; sample < 4; ++sample) {
    expect_events(filter.sample(false, false), {});
  }
}

HOST_TEST(button_boot_short_press_still_pages_forward_after_the_long_press_exists) {
  ButtonFilter filter;
  for (int sample = 0; sample < 3; ++sample) {
    expect_events(filter.sample(false, true), {});
  }
  for (int sample = 0; sample < 2; ++sample) {
    expect_events(filter.sample(false, false), {});
  }
  expect_events(filter.sample(false, false), {ButtonEvent::Next});
  expect_events(filter.sample(false, false), {});
}

// Both long presses must be independent: holding one must not arm the other.
HOST_TEST(button_long_presses_do_not_bleed_between_key_and_boot) {
  ButtonFilter filter;
  for (int sample = 0; sample < 3; ++sample) {
    expect_events(filter.sample(true, false), {});
  }
  for (int sample = 0; sample < static_cast<int>(ButtonFilter::kLongPressSamples) - 2; ++sample) {
    expect_events(filter.sample(true, false), {});
  }
  // KEY crossing its threshold must produce EnterSetup and nothing else.
  expect_events(filter.sample(true, false), {ButtonEvent::EnterSetup});
}

