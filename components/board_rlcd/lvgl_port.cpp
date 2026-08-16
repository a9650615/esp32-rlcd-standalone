#include "lvgl_port.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "board_pins.hpp"
#include "display_port.hpp"

namespace board {
namespace {

constexpr char kTag[] = "board_lvgl";
constexpr uint32_t kTickPeriodMs = 5;
constexpr uint32_t kTaskMaxDelayMs = 500;
constexpr uint32_t kTaskMinDelayMs = 50;
constexpr size_t kBufferBytes =
    static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight) * sizeof(uint16_t);

SemaphoreHandle_t lvgl_mutex = nullptr;
esp_timer_handle_t lvgl_tick_timer = nullptr;
lv_display_t* display_handle = nullptr;
uint8_t* buffer_1 = nullptr;
uint8_t* buffer_2 = nullptr;

// Written only by the LVGL task, read by the OTA rollback guard. Relaxed is
// enough: the reader compares two samples taken seconds apart and only cares
// that the value moved, not which pass it landed on.
std::atomic<uint32_t> lvgl_loops{0};

void increase_lvgl_tick(void*) { lv_tick_inc(kTickPeriodMs); }

#ifndef NDEBUG
// A copy of what was drawn, in plain raster order - one bit per pixel, rows
// top to bottom, MSB leftmost. The panel's own buffer is in the ST7305's
// packed layout behind two lookup tables, so reconstructing an image from it
// would mean reimplementing the controller's addressing; this is the same
// pixels one step earlier, where they are still in the order a PNG wants.
//
// 15000 bytes in PSRAM, debug builds only. It exists so layout work can be
// looked at instead of inferred from geometry logs.
uint8_t* shadow_buffer = nullptr;

// Incremented when a flush completes the bottom-right corner, i.e. when the
// shadow holds a whole frame rather than part of one. LVGL delivers a
// full-screen redraw in several flushes across several handler passes, so
// "the tick after the render" is not when the picture is finished - reading it
// then gave screenshots two page transitions stale.
std::atomic<uint32_t> frames_flushed{0};

void shadow_set_pixel(int x, int y, bool black) {
  if (shadow_buffer == nullptr) return;
  if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) return;
  const size_t index = static_cast<size_t>(y) * (kWidth / 8) + (x / 8);
  const uint8_t mask = static_cast<uint8_t>(0x80U >> (x % 8));
  if (black) {
    shadow_buffer[index] |= mask;
  } else {
    shadow_buffer[index] &= static_cast<uint8_t>(~mask);
  }
}
#endif

void flush_display(lv_display_t* display_handle, const lv_area_t* area,
                   uint8_t* color_p) {
  Display& display = board::display();
  const uint16_t* pixels = reinterpret_cast<const uint16_t*>(color_p);
  for (int y = area->y1; y <= area->y2; ++y) {
    for (int x = area->x1; x <= area->x2; ++x) {
      const bool black = *pixels++ < 0x7fff;
      display.set_pixel(x, y, black ? Color::Black : Color::White);
#ifndef NDEBUG
      shadow_set_pixel(x, y, black);
#endif
    }
  }
  display.refresh();
#ifndef NDEBUG
  if (area->x2 >= kWidth - 1 && area->y2 >= kHeight - 1) {
    frames_flushed.fetch_add(1, std::memory_order_relaxed);
  }
#endif
  lv_display_flush_ready(display_handle);
}

void lvgl_task(void*) {
  uint32_t task_delay_ms = kTaskMaxDelayMs;
  for (;;) {
    if (lvgl_lock(-1)) {
      task_delay_ms = lv_timer_handler();
      lvgl_unlock();
      // Counted after the unlock, so it advances only on a pass that both
      // took the lock and came back out of LVGL - the two things a hung
      // render stops doing.
      lvgl_loops.fetch_add(1, std::memory_order_relaxed);
    }
    if (task_delay_ms > kTaskMaxDelayMs) {
      task_delay_ms = kTaskMaxDelayMs;
    } else if (task_delay_ms < kTaskMinDelayMs) {
      task_delay_ms = kTaskMinDelayMs;
    }
    vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
  }
}

void cleanup_lvgl() {
  if (lvgl_tick_timer != nullptr) {
    (void)esp_timer_stop(lvgl_tick_timer);
    (void)esp_timer_delete(lvgl_tick_timer);
    lvgl_tick_timer = nullptr;
  }
  if (display_handle != nullptr) {
    lv_display_delete(display_handle);
    display_handle = nullptr;
  }
  if (buffer_2 != nullptr) {
    heap_caps_free(buffer_2);
    buffer_2 = nullptr;
  }
  if (buffer_1 != nullptr) {
    heap_caps_free(buffer_1);
    buffer_1 = nullptr;
  }
  if (lvgl_mutex != nullptr) {
    vSemaphoreDelete(lvgl_mutex);
    lvgl_mutex = nullptr;
  }
}

}  // namespace

esp_err_t lvgl_init() {
  if (display_handle != nullptr) {
    return ESP_OK;
  }

  const size_t psram_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  ESP_LOGI(kTag, "PSRAM size: %u bytes, free: %u bytes",
           static_cast<unsigned>(psram_bytes),
           static_cast<unsigned>(psram_free));
  if (psram_bytes == 0) {
    ESP_LOGE(kTag, "fatal: LVGL requires PSRAM");
    return ESP_ERR_NOT_FOUND;
  }

  lvgl_mutex = xSemaphoreCreateMutex();
  if (lvgl_mutex == nullptr) {
    ESP_LOGE(kTag, "LVGL mutex allocation failed");
    return ESP_ERR_NO_MEM;
  }

  lv_init();
  display_handle = lv_display_create(kWidth, kHeight);
  if (display_handle == nullptr) {
    ESP_LOGE(kTag, "LVGL display allocation failed");
    cleanup_lvgl();
    return ESP_ERR_NO_MEM;
  }
  lv_display_set_color_format(display_handle, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display_handle, flush_display);

  buffer_1 = static_cast<uint8_t*>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM));
  if (buffer_1 == nullptr) {
    ESP_LOGE(kTag, "LVGL buffer 1 allocation failed (%u bytes)",
             static_cast<unsigned>(kBufferBytes));
    cleanup_lvgl();
    return ESP_ERR_NO_MEM;
  }
  buffer_2 = static_cast<uint8_t*>(heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM));
  if (buffer_2 == nullptr) {
    ESP_LOGE(kTag, "LVGL buffer 2 allocation failed (%u bytes)",
             static_cast<unsigned>(kBufferBytes));
    cleanup_lvgl();
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(kTag, "display buffer allocation: LVGL RGB565 buffers=%u bytes each",
           static_cast<unsigned>(kBufferBytes));

  lv_display_set_buffers(display_handle, buffer_1, buffer_2, kBufferBytes,
                          LV_DISPLAY_RENDER_MODE_FULL);

  esp_timer_create_args_t tick_args{};
  tick_args.callback = increase_lvgl_tick;
  tick_args.name = "lvgl_tick";
  esp_err_t result = esp_timer_create(&tick_args, &lvgl_tick_timer);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "LVGL tick timer creation failed: %s", esp_err_to_name(result));
    cleanup_lvgl();
    return result;
  }
  result = esp_timer_start_periodic(lvgl_tick_timer, kTickPeriodMs * 1000U);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "LVGL tick timer start failed: %s", esp_err_to_name(result));
    cleanup_lvgl();
    return result;
  }

  const BaseType_t task_result = xTaskCreatePinnedToCore(
      lvgl_task, "LVGL", 8 * 1024, nullptr, 5, nullptr, 0);
  if (task_result != pdPASS) {
    ESP_LOGE(kTag, "LVGL task creation failed");
    cleanup_lvgl();
    return ESP_ERR_NO_MEM;
  }
  #ifndef NDEBUG
  shadow_buffer = static_cast<uint8_t*>(
      heap_caps_calloc(1, kFramebufferSnapshotBytes, MALLOC_CAP_SPIRAM));
  if (shadow_buffer == nullptr) {
    ESP_LOGW(kTag, "screenshot buffer unavailable; snapshots disabled");
  }
#endif
ESP_LOGI(kTag, "LVGL task created on core 0");
  return ESP_OK;
}

bool lvgl_lock(int timeout_ms) {
  if (lvgl_mutex == nullptr) {
    return false;
  }
  const TickType_t timeout_ticks =
      timeout_ms == -1 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTake(lvgl_mutex, timeout_ticks) == pdTRUE;
}

void lvgl_unlock() {
  if (lvgl_mutex != nullptr) {
    (void)xSemaphoreGive(lvgl_mutex);
  }
}

#ifndef NDEBUG
uint32_t lvgl_frame_count() {
  return frames_flushed.load(std::memory_order_relaxed);
}

bool framebuffer_snapshot(uint8_t* out, size_t length) {
  if (shadow_buffer == nullptr || out == nullptr ||
      length < kFramebufferSnapshotBytes) {
    return false;
  }
  std::memcpy(out, shadow_buffer, kFramebufferSnapshotBytes);
  return true;
}
#endif

uint32_t lvgl_loop_count() {
  return lvgl_loops.load(std::memory_order_relaxed);
}

}  // namespace board
