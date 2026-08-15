#pragma once

#include <cstddef>
#include <cstdint>

namespace app_core {

inline constexpr size_t kPageCount = 5;
inline constexpr uint64_t kManualPauseMs = 60'000;

struct CarouselState {
  size_t index = 0;
  uint64_t page_started_ms = 0;
  uint64_t manual_until_ms = 0;
  bool manual_mode = false;
};

struct Transition {
  CarouselState state;
  bool page_changed = false;
};

namespace carousel {

Transition tick(CarouselState state, uint64_t now_ms, uint8_t dwell_seconds);
Transition next(CarouselState state, uint64_t now_ms);
Transition previous(CarouselState state, uint64_t now_ms);

}  // namespace carousel
}  // namespace app_core
