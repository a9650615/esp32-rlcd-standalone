#include "lvgl_port.hpp"

#include <cstdint>

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

void increase_lvgl_tick(void*) { lv_tick_inc(kTickPeriodMs); }

void flush_display(lv_display_t* display_handle, const lv_area_t* area,
                   uint8_t* color_p) {
  Display& display = board::display();
  const uint16_t* pixels = reinterpret_cast<const uint16_t*>(color_p);
  for (int y = area->y1; y <= area->y2; ++y) {
    for (int x = area->x1; x <= area->x2; ++x) {
      display.set_pixel(x, y, *pixels++ < 0x7fff ? Color::Black : Color::White);
    }
  }
  display.refresh();
  lv_display_flush_ready(display_handle);
}

void lvgl_task(void*) {
  uint32_t task_delay_ms = kTaskMaxDelayMs;
  for (;;) {
    if (lvgl_lock(-1)) {
      task_delay_ms = lv_timer_handler();
      lvgl_unlock();
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

}  // namespace board
