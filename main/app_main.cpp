#include "app_snapshot.hpp"
#include "battery.hpp"
#include "board_buttons.hpp"
#include "board_pins.hpp"
#include "display_port.hpp"
#include "lvgl_port.hpp"
#include "ui_app.hpp"
#include "wifi_provision.hpp"

#include <cstdio>
#include <cstring>

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr char kTag[] = "app_main";
constexpr gpio_num_t kRtcSda = GPIO_NUM_13;
constexpr gpio_num_t kRtcScl = GPIO_NUM_14;
constexpr uint8_t kRtcAddress = 0x51;
constexpr uint8_t kRtcSecondsRegister = 0x04;

app_core::RtcDateTime compile_clock() {
  app_core::RtcDateTime result{};
  char month[4]{};
  unsigned day = 1;
  unsigned year = 2000;
  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
  (void)std::sscanf(__DATE__, "%3s %u %u", month, &day, &year);
  (void)std::sscanf(__TIME__, "%u:%u:%u", &hour, &minute, &second);
  static constexpr const char* names[] = {"Jan", "Feb", "Mar", "Apr",
                                           "May", "Jun", "Jul", "Aug",
                                           "Sep", "Oct", "Nov", "Dec"};
  for (uint8_t index = 0; index < 12; ++index) {
    if (std::strncmp(month, names[index], 3) == 0) {
      result.month = static_cast<uint8_t>(index + 1);
      break;
    }
  }
  result.year = static_cast<uint16_t>(year);
  result.day = static_cast<uint8_t>(day);
  result.hour = static_cast<uint8_t>(hour);
  result.minute = static_cast<uint8_t>(minute);
  result.second = static_cast<uint8_t>(second);
  return result;
}

bool read_rtc(app_core::RtcDateTime& clock) {
  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = kRtcSda;
  bus_config.scl_io_num = kRtcScl;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;

  i2c_master_bus_handle_t bus = nullptr;
  esp_err_t result = i2c_new_master_bus(&bus_config, &bus);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "RTC probe bus unavailable: %s", esp_err_to_name(result));
    return false;
  }

  i2c_device_config_t device_config{};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = kRtcAddress;
  device_config.scl_speed_hz = 100'000;
  i2c_master_dev_handle_t device = nullptr;
  result = i2c_master_bus_add_device(bus, &device_config, &device);
  if (result != ESP_OK) {
    (void)i2c_del_master_bus(bus);
    ESP_LOGW(kTag, "RTC probe device unavailable: %s", esp_err_to_name(result));
    return false;
  }

  // PCF85063 register-pointer selection followed by a receive is read-only:
  // no RTC register is ever written by this probe.
  uint8_t register_pointer = kRtcSecondsRegister;
  uint8_t registers[7]{};
  result = i2c_master_transmit_receive(device, &register_pointer,
                                       sizeof(register_pointer), registers,
                                       sizeof(registers), 100);
  (void)i2c_master_bus_rm_device(device);
  (void)i2c_del_master_bus(bus);
  if (result != ESP_OK || (registers[0] & 0x80U) != 0 ||
      !app_core::decode_pcf85063(registers, sizeof(registers), clock)) {
    ESP_LOGW(kTag, "RTC absent or invalid; using compile-time fallback");
    return false;
  }
  return true;
}

[[noreturn]] void fatal_loop(const char* reason, esp_err_t error) {
  ESP_LOGE(kTag, "fatal: %s (%s); startup stopped", reason,
           esp_err_to_name(error));
  for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

constexpr uint32_t kBatterySamplePeriodMs = 30'000;

// Samples the battery divider roughly every 30 s and publishes it through
// wifi_provision's existing snapshot owner; never touches lv_* directly.
[[noreturn]] void battery_monitor_task(void*) {
  // Edge-triggered so a persisting condition logs once, not every 30 s.
  bool was_warning = false;
  bool was_danger = false;
  for (;;) {
    app_core::BatteryData battery;
    if (board::battery_read(battery)) {
      // The screen shows percent only, but CONFIG_BATTERY_CALIBRATION_PERMILLE
      // is tuned by comparing millivolts against a multimeter, so the raw
      // figure has to be reachable somewhere.
      ESP_LOGI(kTag, "battery valid=%d mV=%d percent=%u", battery.valid,
               battery.millivolts, battery.percent);

      battery.overvoltage_warning =
          app_core::battery_overvoltage_warning(battery.millivolts);
      const bool danger = app_core::battery_overvoltage_danger(battery.millivolts);

      // Detection only: this board has no charger-enable GPIO, so firmware
      // cannot stop or limit charging - these are warnings, not protection.
      if (danger && !was_danger) {
        ESP_LOGE(kTag,
                 "battery overvoltage danger: %d mV (limit %d mV); firmware "
                 "cannot stop charging on this board",
                 battery.millivolts, app_core::kBatteryOvervoltageDangerMillivolts);
      } else if (!danger && was_danger) {
        ESP_LOGI(kTag, "battery overvoltage danger cleared: %d mV",
                 battery.millivolts);
      }
      if (battery.overvoltage_warning && !was_warning) {
        ESP_LOGW(kTag, "battery overvoltage warning: %d mV (limit %d mV)",
                 battery.millivolts, app_core::kBatteryOvervoltageWarningMillivolts);
      } else if (!battery.overvoltage_warning && was_warning) {
        ESP_LOGI(kTag, "battery overvoltage warning cleared: %d mV",
                 battery.millivolts);
      }
      was_warning = battery.overvoltage_warning;
      was_danger = danger;

      wifi_provision::set_battery(battery);
    } else {
      ESP_LOGW(kTag, "battery ADC read failed");
    }
    vTaskDelay(pdMS_TO_TICKS(kBatterySamplePeriodMs));
  }
}

}  // namespace

extern "C" void app_main() {
  const size_t psram_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  ESP_LOGI(kTag, "startup diagnostics PSRAM bytes=%u",
           static_cast<unsigned>(psram_bytes));
  if (psram_bytes == 0) fatal_loop("required PSRAM unavailable", ESP_ERR_NOT_FOUND);

  esp_err_t result = board::display_init();
  if (result != ESP_OK) fatal_loop("display initialization failed", result);
  ESP_LOGI(kTag, "startup diagnostics display=ready");

  result = board::lvgl_init();
  if (result != ESP_OK) fatal_loop("LVGL initialization failed", result);
  ESP_LOGI(kTag, "startup diagnostics LVGL=ready");

  app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  app_core::RtcDateTime clock = compile_clock();
  const bool rtc_ok = read_rtc(clock);
  if (rtc_ok) {
    ESP_LOGI(kTag, "RTC PCF85063 valid %04u-%02u-%02u %02u:%02u:%02u",
             clock.year, clock.month, clock.day, clock.hour, clock.minute,
             clock.second);
  } else {
    ESP_LOGW(kTag, "RTC fallback source=compile date/time");
  }

  result = board::buttons_start();
  if (result != ESP_OK) fatal_loop("button initialization failed", result);
  ESP_LOGI(kTag, "startup diagnostics buttons=ready GPIO0=input/pull-up");

  if (!ui::start(snapshot, clock, !rtc_ok)) {
    fatal_loop("UI lifecycle initialization failed", ESP_FAIL);
  }
  ESP_LOGI(kTag, "startup diagnostics registry=ready cycle=1");

  ui::set_setup_gesture_handler(&wifi_provision::toggle_setup);
  result = wifi_provision::start(snapshot);
  if (result != ESP_OK) {
    // Non-fatal: the carousel already runs standalone without Wi-Fi.
    ESP_LOGE(kTag, "Wi-Fi provisioning startup failed: %s",
             esp_err_to_name(result));
  }

  if (xTaskCreate(&battery_monitor_task, "battery_monitor", 3072, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    // Non-fatal: the carousel and Wi-Fi already run without battery data.
    ESP_LOGE(kTag, "battery monitor task creation failed");
  }
}
