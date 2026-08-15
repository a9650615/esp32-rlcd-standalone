#include "app_snapshot.hpp"
#include "battery.hpp"
#include "board_buttons.hpp"
#include "board_i2c.hpp"
#include "board_pins.hpp"
#include "display_port.hpp"
#include "lvgl_port.hpp"
#include "market.hpp"
#include "net_time.hpp"
#include "shtc3.hpp"
#include "ui_app.hpp"
#include "weather.hpp"
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
  // Shared bus (SDA13/SCL14): SHTC3 sits on the same lines at 0x70, so this
  // owns the bus for the app's lifetime via board_i2c instead of creating
  // (and tearing down) a second, competing bus master.
  esp_err_t result = board::board_i2c_init();
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "RTC probe bus unavailable: %s", esp_err_to_name(result));
    return false;
  }

  i2c_master_dev_handle_t device = nullptr;
  result = board::board_i2c_add_device(kRtcAddress, 100'000, device);
  if (result != ESP_OK) {
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

constexpr uint32_t kIndoorSamplePeriodMs = 60'000;

// Samples the SHTC3 roughly every 60 s - climate moves slowly and this
// panel's refresh is expensive - and publishes it through wifi_provision's
// existing snapshot owner; never touches lv_* directly. Always publishes,
// even on failure, with a freshly default-constructed IndoorData (valid
// stays false): a read/CRC failure must flip the page to NO DATA, not leave
// whatever the last valid reading was sitting on screen as though current.
[[noreturn]] void indoor_monitor_task(void*) {
  for (;;) {
    app_core::IndoorData indoor;
    float temperature_c = 0.0f;
    float humidity_percent = 0.0f;
    if (board::shtc3_read(temperature_c, humidity_percent)) {
      indoor.valid = true;
      indoor.temperature_c = temperature_c;
      indoor.humidity_percent =
          static_cast<uint8_t>(humidity_percent + 0.5f);
      ESP_LOGI(kTag, "indoor valid temp_c=%.1f humidity=%u",
               indoor.temperature_c, indoor.humidity_percent);
    } else {
      ESP_LOGW(kTag, "SHTC3 read failed");
    }
    wifi_provision::set_indoor(indoor);
    vTaskDelay(pdMS_TO_TICKS(kIndoorSamplePeriodMs));
  }
}

// Refreshes on the interval weather.hpp itself defines (30 min) and
// publishes weather::current() unconditionally: on a failed refresh,
// current() already applies the component's own valid/stale rules (a
// previously-successful cached reading stays valid and goes stale; only a
// never-successful fetch is invalid), so there is nothing extra to decide
// here. This provider covers one location (see weather::refresh()'s IP
// geolocation / manual override); AppSnapshot::new_york_weather is left
// untouched (stays at its default-invalid state) rather than duplicating
// this one reading into a second "city" that was never actually fetched.
[[noreturn]] void weather_monitor_task(void*) {
  for (;;) {
    if (!weather::refresh()) {
      ESP_LOGW(kTag, "weather refresh failed; serving cached/stale reading");
    }
    wifi_provision::set_weather(weather::current());
    vTaskDelay(pdMS_TO_TICKS(weather::kRefreshIntervalSeconds * 1000));
  }
}

// Refreshes both markets on the interval market.hpp itself defines (30 min).
// refresh_taiwan()/refresh_us() already set their own cache to invalid on
// any failure (network, bad shape, rate limit) rather than leaving a stale
// or substituted value, so taiwan()/us() are safe to publish unconditionally
// right after each refresh call.
[[noreturn]] void market_monitor_task(void*) {
  for (;;) {
    if (!market::refresh_taiwan()) {
      ESP_LOGW(kTag, "Taiwan market refresh failed");
    }
    wifi_provision::set_taiwan_market(market::taiwan());
    if (!market::refresh_us()) {
      ESP_LOGW(kTag, "US market refresh failed");
    }
    wifi_provision::set_us_market(market::us());
    vTaskDelay(pdMS_TO_TICKS(market::kRefreshIntervalSeconds * 1000));
  }
}

constexpr uint32_t kNetTimeCheckPeriodMs = 60'000;

// 1 Jan 2000 was a Saturday; days-since then mod 7 gives the weekday. Local
// to this file - components/ui's own copy of this formatting isn't a public
// API - so this duplicates a handful of lines rather than reaching across
// the ownership boundary for them.
const char* weekday_name(const app_core::RtcDateTime& date) {
  uint64_t days = 0;
  for (uint16_t year = 2000; year < date.year; ++year) {
    days += (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
  }
  for (uint8_t month = 1; month < date.month; ++month) {
    days += app_core::days_in_month(date.year, month);
  }
  days += date.day - 1;
  static constexpr const char* names[] = {"Sun", "Mon", "Tue", "Wed",
                                          "Thu", "Fri", "Sat"};
  return names[(6 + days) % 7];
}

void format_clock(const app_core::RtcDateTime& clock, app_core::ClockData& out) {
  static constexpr const char* month_names[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char hero[8];
  char date[32];
  std::snprintf(hero, sizeof(hero), "%02u:%02u", clock.hour, clock.minute);
  std::snprintf(date, sizeof(date), "%s, %02u %s %04u", weekday_name(clock),
                clock.day, month_names[clock.month - 1], clock.year);
  out.hero = hero;
  out.date = date;
  // Must match ui_data.hpp's compact_clock_source() exactly, or the tray
  // silently falls through to UNKNOWN with no warning.
  out.source = "SNTP";
}

// Polls net_time::synced() roughly once a minute - the visible clock itself
// only ever repaints on a minute rollover, so anything faster buys nothing.
// Publishes only once synced; before that this does nothing at all, leaving
// the RTC/compile-time fallback clock (and its own honest source string)
// exactly as already set at startup.
[[noreturn]] void net_time_monitor_task(void*) {
  for (;;) {
    app_core::RtcDateTime clock{};
    if (net_time::synced() && net_time::now(clock)) {
      app_core::ClockData data;
      format_clock(clock, data);
      wifi_provision::set_clock(data);
    }
    vTaskDelay(pdMS_TO_TICKS(kNetTimeCheckPeriodMs));
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

  // Depends on the esp_netif/event-loop init wifi_provision::start() just
  // performed. Safe to call before the station has an IP - SNTP just queues
  // requests until Wi-Fi comes up.
  result = net_time::start();
  if (result != ESP_OK) {
    // Non-fatal: the clock keeps showing the RTC/compile-time fallback.
    ESP_LOGE(kTag, "net_time startup failed: %s", esp_err_to_name(result));
  }

  if (xTaskCreate(&battery_monitor_task, "battery_monitor", 3072, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    // Non-fatal: the carousel and Wi-Fi already run without battery data.
    ESP_LOGE(kTag, "battery monitor task creation failed");
  }

  // 3072 B, matching battery_monitor_task: I2C-only, no TLS, no JSON - the
  // same modest headroom that task already runs safely on.
  if (xTaskCreate(&indoor_monitor_task, "indoor_monitor", 3072, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag, "indoor monitor task creation failed");
  }

  // 3072 B: RTC-style date math and snprintf only, same shape as
  // battery_monitor_task - no TLS, no JSON, no large buffers.
  if (xTaskCreate(&net_time_monitor_task, "net_time_monitor", 3072, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag, "net_time monitor task creation failed");
  }

  // 16384 B: weather::refresh() itself puts an 8 KiB response buffer
  // (kForecastBufferBytes) on the calling task's stack, on top of which
  // esp_http_client's TLS handshake (mbedTLS) and cJSON parsing add their
  // own several-KiB of depth. Doubling the raw buffer size is the margin.
  if (xTaskCreate(&weather_monitor_task, "weather_monitor", 16384, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag, "weather monitor task creation failed");
  }

  // 8192 B: market::http_get() heap-allocates its response body (std::string),
  // so unlike weather this task's stack only has to cover the TLS handshake
  // and JSON parsing depth for two sequential HTTPS requests, not a large
  // local buffer.
  if (xTaskCreate(&market_monitor_task, "market_monitor", 8192, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag, "market monitor task creation failed");
  }
}
