#pragma once

#include <cstdint>

#include <esp_err.h>

namespace board {

esp_err_t lvgl_init();
bool lvgl_lock(int timeout_ms = -1);
void lvgl_unlock();

// Monotonic count of completed lv_timer_handler() passes. Sampled twice over
// an interval, this is proof the LVGL task is genuinely turning rather than
// merely existing: it only advances when the task both acquires the lock and
// returns from LVGL, which is exactly the pair that a hung render breaks.
//
// The OTA rollback guard needs this because the task watchdog on this board is
// configured without panic, so a hang produces log noise forever instead of
// the reset that would otherwise trigger a rollback.
uint32_t lvgl_loop_count();

}  // namespace board
