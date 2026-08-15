#include "board_buttons.hpp"

#ifdef ESP_PLATFORM

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/task.h>

#include "board_pins.hpp"

namespace board {
namespace {

constexpr char kTag[] = "board_buttons";
constexpr UBaseType_t kQueueLength = 8;
constexpr TickType_t kPollPeriod = pdMS_TO_TICKS(ButtonFilter::kSamplePeriodMs);

StaticQueue_t g_queue_storage;
uint8_t g_queue_buffer[kQueueLength * sizeof(ButtonEvent)];
QueueHandle_t g_queue = nullptr;
ButtonFilter g_filter;

void button_poll_task(void*) {
  for (;;) {
    const ButtonFilter::Events events =
        g_filter.sample(gpio_get_level(kKey) == 0, gpio_get_level(kBoot) == 0);
    for (std::size_t index = 0; index < events.count; ++index) {
      if (xQueueSend(g_queue, &events.events[index], 0) != pdTRUE) {
        ESP_LOGW(kTag, "button event queue full; dropping newest event");
      }
    }
    vTaskDelay(kPollPeriod);
  }
}

}  // namespace

esp_err_t buttons_start() {
  if (g_queue != nullptr) {
    return ESP_OK;
  }

  g_queue = xQueueCreateStatic(kQueueLength, sizeof(ButtonEvent),
                               g_queue_buffer, &g_queue_storage);
  if (g_queue == nullptr) {
    ESP_LOGE(kTag, "failed to create button event queue");
    return ESP_ERR_NO_MEM;
  }

  gpio_config_t input_config{};
  input_config.intr_type = GPIO_INTR_DISABLE;
  input_config.mode = GPIO_MODE_INPUT;
  // ROM download strap; short-release only; never add pulldown or long-press ownership.
  input_config.pin_bit_mask = (1ULL << kKey) | (1ULL << kBoot);
  input_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  input_config.pull_up_en = GPIO_PULLUP_ENABLE;
  const esp_err_t gpio_result = gpio_config(&input_config);
  if (gpio_result != ESP_OK) {
    ESP_LOGE(kTag, "button GPIO configuration failed: %s",
             esp_err_to_name(gpio_result));
    g_queue = nullptr;
    return gpio_result;
  }
  if (gpio_set_pull_mode(kKey, GPIO_PULLUP_ONLY) != ESP_OK ||
      gpio_set_pull_mode(kBoot, GPIO_PULLUP_ONLY) != ESP_OK) {
    ESP_LOGE(kTag, "button pull-up configuration failed");
    g_queue = nullptr;
    return ESP_FAIL;
  }

  const BaseType_t task_result =
      xTaskCreate(button_poll_task, "button_poll", 2048, nullptr, 5, nullptr);
  if (task_result != pdPASS) {
    ESP_LOGE(kTag, "failed to start button polling task");
    g_queue = nullptr;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

QueueHandle_t button_event_queue() { return g_queue; }

}  // namespace board

#endif  // ESP_PLATFORM
