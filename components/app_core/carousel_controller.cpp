#include "carousel_controller.hpp"

namespace app_core::carousel {
namespace {

uint64_t pause_until(uint64_t now_ms) {
  return now_ms > UINT64_MAX - kManualPauseMs ? UINT64_MAX
                                               : now_ms + kManualPauseMs;
}

Transition manual_transition(CarouselState state, uint64_t now_ms, size_t index,
                             size_t page_count) {
  if (page_count == 0) return {state, false};
  state.index = index;
  state.page_started_ms = now_ms;
  state.manual_until_ms = pause_until(now_ms);
  state.manual_mode = true;
  return {state, true};
}

}  // namespace

Transition tick(CarouselState state, uint64_t now_ms, uint8_t dwell_seconds,
                size_t page_count) {
  if (page_count == 0) return {state, false};

  if (state.manual_mode) {
    if (now_ms < state.manual_until_ms) return {state, false};
    state.index = 0;
    state.page_started_ms = now_ms;
    state.manual_until_ms = 0;
    state.manual_mode = false;
    return {state, true};
  }

  const uint64_t dwell_ms = static_cast<uint64_t>(dwell_seconds) * 1000;
  if (now_ms < state.page_started_ms ||
      now_ms - state.page_started_ms < dwell_ms) {
    return {state, false};
  }

  state.index = (state.index + 1) % page_count;
  state.page_started_ms = now_ms;
  return {state, true};
}

Transition next(CarouselState state, uint64_t now_ms, size_t page_count) {
  if (page_count == 0) return {state, false};
  return manual_transition(state, now_ms,
                           (state.index + 1) % page_count, page_count);
}

Transition previous(CarouselState state, uint64_t now_ms, size_t page_count) {
  if (page_count == 0) return {state, false};
  return manual_transition(state, now_ms,
                           state.index == 0 ? page_count - 1 : state.index - 1,
                           page_count);
}

}  // namespace app_core::carousel
