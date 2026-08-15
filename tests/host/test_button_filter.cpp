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
