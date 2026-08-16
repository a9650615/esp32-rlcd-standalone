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

#ifndef NDEBUG
// 400x300 at one bit per pixel, rows top to bottom, MSB leftmost, 1 = black.
inline constexpr size_t kFramebufferSnapshotBytes = 400 * 300 / 8;

// Copies the last frame drawn. Debug builds only - this exists so panel layout
// can be looked at rather than inferred from geometry logs, which cannot say
// whether something is ugly, only whether it is out of bounds.
//
// False before the first frame, or if `length` is short.
bool framebuffer_snapshot(uint8_t* out, size_t length);

// Completed whole frames since boot. A full-screen redraw arrives as several
// partial flushes, so this counts only the one that finishes the bottom-right
// corner - wait for it to advance before trusting a snapshot.
uint32_t lvgl_frame_count();
#endif

}  // namespace board
