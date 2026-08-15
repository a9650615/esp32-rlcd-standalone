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

// EnterSetup is a KEY long press, OpenMenu a BOOT long press.
//
// Taking BOOT long presses is safe despite GPIO0 being the download strap: the
// strap is sampled at reset, not while running. The only cost is that a reset
// arriving while BOOT happens to be held lands in the ROM downloader, which a
// power cycle clears.
enum class ButtonEvent : uint8_t { Next, Previous, EnterSetup, OpenMenu };

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
    update(boot_, boot_active_low, ButtonEvent::Next, events, &boot_long_press_fired_);

    hold(key_, key_held_samples_, key_long_press_fired_, ButtonEvent::EnterSetup,
         events);
    hold(boot_, boot_held_samples_, boot_long_press_fired_,
         ButtonEvent::OpenMenu, events);
    return events;
  }

 private:
  struct State {
    bool stable_pressed = false;
    bool candidate_pressed = false;
    std::size_t candidate_samples = 0;
  };

  // Fires `event` once, on the sample where the hold crosses the threshold,
  // and arms the release suppression so the same press does not also emit its
  // short-press event. Shared by both buttons rather than written twice: the
  // two long presses have to behave identically or the same hold means
  // different things depending on which button it was.
  static void hold(const State& state, std::size_t& held_samples, bool& fired,
                   ButtonEvent event, Events& events) {
    if (!state.stable_pressed) {
      held_samples = 0;
      return;
    }
    if (held_samples >= kLongPressSamples) return;
    ++held_samples;
    if (held_samples == kLongPressSamples &&
        events.count < events.events.size()) {
      events.events[events.count++] = event;
      fired = true;
    }
  }

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
  std::size_t boot_held_samples_ = 0;
  bool key_long_press_fired_ = false;
  bool boot_long_press_fired_ = false;
};

#ifdef ESP_PLATFORM
// Initializes the input pins and starts the polling task. Call only after
// PSRAM, display, and LVGL startup has completed.
esp_err_t buttons_start();
QueueHandle_t button_event_queue();

#endif  // ESP_PLATFORM

}  // namespace board
