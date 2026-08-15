#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" void app_main() {
  ESP_LOGI("rlcd_smoke", "RLCD board-layer smoke firmware; no hardware initialization");
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
