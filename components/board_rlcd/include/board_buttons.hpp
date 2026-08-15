#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#ifdef ESP_PLATFORM

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#endif  // ESP_PLATFORM

namespace board {

enum class ButtonEvent : uint8_t { Next, Previous, EnterSetup };

// A dependency-free active-low debounce filter. Call sample() once per 10 ms.
class ButtonFilter {
 public:
  static constexpr uint32_t kSamplePeriodMs = 10;
  static constexpr uint32_t kStableThresholdMs = 30;
  static constexpr std::size_t kStableSamples =
      kStableThresholdMs / kSamplePeriodMs;
  static_assert(kStableSamples == 3, "button debounce must remain 30 ms");

  // KEY-only long press: held this long opens/closes Wi-Fi setup mode.
  static constexpr uint32_t kLongPressMs = 2000;
  static constexpr std::size_t kLongPressSamples =
      kLongPressMs / kSamplePeriodMs;
  static_assert(kLongPressSamples == 200, "long press threshold must remain 2000 ms");

  struct Events {
    std::array<ButtonEvent, 2> events{};
    std::size_t count = 0;
  };

  // key_active_low and boot_active_low are true while their pins read low.
  Events sample(bool key_active_low, bool boot_active_low) {
    Events events;
    update(key_, key_active_low, ButtonEvent::Previous, events, &key_long_press_fired_);
    update(boot_, boot_active_low, ButtonEvent::Next, events);

    if (key_.stable_pressed) {
      if (key_held_samples_ < kLongPressSamples) {
        ++key_held_samples_;
        if (key_held_samples_ == kLongPressSamples && events.count < events.events.size()) {
          events.events[events.count++] = ButtonEvent::EnterSetup;
          key_long_press_fired_ = true;
        }
      }
    } else {
      key_held_samples_ = 0;
    }
    return events;
  }

 private:
  struct State {
    bool stable_pressed = false;
    bool candidate_pressed = false;
    std::size_t candidate_samples = 0;
  };

  // suppress_release, when non-null, skips the release event exactly once
  // (used so a fired long press doesn't also emit the short-press event).
  static void update(State& state, bool pressed, ButtonEvent event,
                     Events& events, bool* suppress_release = nullptr) {
    if (pressed == state.stable_pressed) {
      state.candidate_samples = 0;
      return;
    }

    if (pressed != state.candidate_pressed) {
      state.candidate_pressed = pressed;
      state.candidate_samples = 1;
    } else {
      ++state.candidate_samples;
    }

    if (state.candidate_samples < kStableSamples) {
      return;
    }

    const bool was_pressed = state.stable_pressed;
    state.stable_pressed = state.candidate_pressed;
    state.candidate_samples = 0;
    if (was_pressed && !state.stable_pressed && events.count < events.events.size()) {
      if (suppress_release == nullptr || !*suppress_release) {
        events.events[events.count++] = event;
      }
      if (suppress_release != nullptr) {
        *suppress_release = false;
      }
    }
  }

  State key_;
  State boot_;
  std::size_t key_held_samples_ = 0;
  bool key_long_press_fired_ = false;
};

#ifdef ESP_PLATFORM
// Initializes the input pins and starts the polling task. Call only after
// PSRAM, display, and LVGL startup has completed.
esp_err_t buttons_start();
QueueHandle_t button_event_queue();

#endif  // ESP_PLATFORM

}  // namespace board
