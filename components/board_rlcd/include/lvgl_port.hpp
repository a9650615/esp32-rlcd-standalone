#pragma once

#include <cstdint>

#include <esp_err.h>

namespace board {

esp_err_t lvgl_init();
bool lvgl_lock(int timeout_ms = -1);
void lvgl_unlock();

}  // namespace board
