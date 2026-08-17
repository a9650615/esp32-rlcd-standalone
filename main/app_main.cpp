#include "airplay.hpp"
#include "app_snapshot.hpp"
#include "audio.hpp"
#include "battery.hpp"
#include "board_buttons.hpp"
#include "board_i2c.hpp"
#include "board_pins.hpp"
#include "display_port.hpp"
#include "lvgl_port.hpp"
#include "market.hpp"
#include "market_schedule.hpp"
#include "net_log.hpp"
#include "net_time.hpp"
#include "ota.hpp"
#include "ota_confirm.hpp"
#include <nvs.h>
#include <nvs_flash.h>

#include "history.hpp"
#include "history_store.hpp"
#include "ota_pull.hpp"
#include "ota_release.hpp"
#include "ota_session.hpp"
#include "shtc3.hpp"
#include "ui_app.hpp"
#include "weather.hpp"
#include "wifi_provision.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
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

// Puts network time into the RTC so the next boot does not need a network.
//
// Nothing wrote this chip before, which is why every boot logged "RTC absent
// or invalid": bit 7 of the seconds register is the oscillator-stop flag, the
// PCF85063 sets it when it has lost timekeeping, and it clears only on a
// write. A chip that has never been written therefore reads as invalid
// forever, and the board fell back to its build timestamp - which is how a
// freshly flashed device shows the time the firmware was compiled.
bool write_rtc(const app_core::RtcDateTime& clock) {
  uint8_t registers[7]{};
  if (!app_core::encode_pcf85063(clock, registers, sizeof(registers))) {
    ESP_LOGW(kTag, "RTC write refused: %04u-%02u-%02u %02u:%02u:%02u is out of range",
             clock.year, clock.month, clock.day, clock.hour, clock.minute,
             clock.second);
    return false;
  }
  if (board::board_i2c_init() != ESP_OK) return false;
  i2c_master_dev_handle_t device = nullptr;
  if (board::board_i2c_add_device(kRtcAddress, 100'000, device) != ESP_OK) {
    return false;
  }
  // Register pointer followed by the seven values, in one transaction: the
  // chip auto-increments, and splitting it would let the seconds roll over
  // between writes.
  uint8_t payload[8];
  payload[0] = kRtcSecondsRegister;
  std::memcpy(payload + 1, registers, sizeof(registers));
  const esp_err_t result =
      i2c_master_transmit(device, payload, sizeof(payload), 100);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "RTC write failed: %s", esp_err_to_name(result));
    return false;
  }
  ESP_LOGI(kTag, "RTC set from network time: %04u-%02u-%02u %02u:%02u:%02u",
           clock.year, clock.month, clock.day, clock.hour, clock.minute,
           clock.second);
  return true;
}

[[noreturn]] void fatal_loop(const char* reason, esp_err_t error) {
  ESP_LOGE(kTag, "fatal: %s (%s); startup stopped", reason,
           esp_err_to_name(error));
  // A freshly written image that cannot finish startup is exactly what
  // rollback exists for, and spinning here would defeat it: this loop never
  // resets, the task watchdog on this board never panics, so the bad image
  // would hold the boot slot forever. Roll back instead - the board returns on
  // the previous firmware, which then reports UPDATE ROLLED BACK on the panel.
  //
  // Nothing is drawn here directly: this runs on the app_main task, and most
  // fatal paths are reached before the display or the snapshot publisher
  // exist. Serial is the only channel for this boot; the panel gets the story
  // on the next one.
  bool readable = false;
  if (ota::pending_verify(readable) && readable) {
    ota::rollback_and_reboot();
  }
  // Not a pending image, or nothing to roll back to. Halt rather than reboot,
  // so a genuinely broken board stays diagnosable over serial instead of
  // becoming a boot loop.
  for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

// How long a freshly written image has to prove the LVGL loop is turning
// before it is accepted. Long enough to cover display/LVGL bring-up and the
// first renders, short enough that the board does not sit in a state where an
// unrelated reset would roll back a perfectly good image.
constexpr uint32_t kOtaVerifyWindowMs = 30'000;
// Association plus DHCP, with room for a slow access point. Generous on
// purpose: this window elapsing means rolling back an image that may be fine,
// so it should only expire when the network is genuinely not coming back.
constexpr uint32_t kOtaVerifyNetworkMs = 90'000;
constexpr uint32_t kOtaVerifyPollMs = 2'000;
constexpr uint32_t kOtaVerifySettleMs = 2'000;

// Runs once at boot and exits. Two jobs, both of which exist because this
// board cannot rely on the usual mechanism: the task watchdog is configured
// without panic (CONFIG_ESP_TASK_WDT_PANIC unset), so a hung image logs
// forever instead of resetting, and an image that never resets is never rolled
// back by the bootloader either.
//
// 1. Surface a previously rejected update on the panel. Otherwise a rollback
//    is completely invisible: the board comes back up looking normal, running
//    older firmware than the user believes they installed.
// 2. Decide the fate of a pending image from positive evidence that the LVGL
//    render loop advanced, and act on that decision here rather than waiting
//    for a reset that this board will never produce on its own.
void ota_guard_task(void*) {
  app_core::OtaData status;
  if (ota::update_was_rejected()) {
    status.phase = app_core::OtaPhase::RolledBack;
    status.detail = "Running " + ota::running_slot_name();
    ESP_LOGW(kTag, "a previous update was rejected; running slot=%s",
             ota::running_slot_name().c_str());
    wifi_provision::set_ota(status);
  }

  bool readable = false;
  const bool pending = ota::pending_verify(readable);
  ESP_LOGI(kTag, "ota guard: slot=%s readable=%d pending_verify=%d",
           ota::running_slot_name().c_str(), readable, pending);
  if (!readable || !pending) {
    // Steady state, including every factory boot. rollback_decision() would
    // say None here too; short-circuiting just avoids holding the task alive
    // for 30 s to reach the same answer.
    vTaskDelete(nullptr);
    return;
  }

  status.phase = app_core::OtaPhase::Verifying;
  status.detail.clear();
  wifi_provision::set_ota(status);

  // Settle first: sampling the counter the instant this task starts can catch
  // the LVGL task before its first pass and read a false stall.
  vTaskDelay(pdMS_TO_TICKS(kOtaVerifySettleMs));
  const uint32_t before = board::lvgl_loop_count();
  vTaskDelay(pdMS_TO_TICKS(kOtaVerifyWindowMs));
  const uint32_t after = board::lvgl_loop_count();
  const bool renders = after != before;
  ESP_LOGI(kTag, "ota guard: lvgl loops %u -> %u renders=%d",
           static_cast<unsigned>(before), static_cast<unsigned>(after),
           renders);

  // Polled rather than waited on an event: this task already owns a timeline
  // and station_has_ip() is the same flag every provider gates its first fetch
  // on, so there is nothing to subscribe to that is not already published.
  bool reachable = false;
  for (uint32_t waited = 0; waited < kOtaVerifyNetworkMs;
       waited += kOtaVerifyPollMs) {
    if (wifi_provision::station_has_ip()) {
      reachable = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(kOtaVerifyPollMs));
  }
  ESP_LOGI(kTag, "ota guard: reachable=%d", reachable);

  switch (ota::rollback_decision(readable, pending, renders, reachable)) {
    case ota::RollbackDecision::MarkValid:
      if (ota::mark_valid() == ESP_OK) {
        status.phase = app_core::OtaPhase::Idle;
        status.detail.clear();
        wifi_provision::set_ota(status);
      }
      break;
    case ota::RollbackDecision::Rollback:
      status.phase = app_core::OtaPhase::Failed;
      status.detail = "Rolling back";
      wifi_provision::set_ota(status);
      // Does not return unless there is nothing to roll back to.
      ota::rollback_and_reboot();
      break;
    case ota::RollbackDecision::None:
      break;
  }
  vTaskDelete(nullptr);
}

// Runs one release check and reports the outcome to the settings page. Its own
// task because the check is a blocking HTTPS round trip and the caller is the
// LVGL thread - doing it inline would freeze the display for the duration and,
// on a slow network, trip the watchdog.
constexpr char kUiNamespace[] = "ui_prefs";
constexpr char kLanguageKey[] = "lang";

// Read before the first render, so a device set to Chinese comes back in
// Chinese instead of showing a frame of English and then flipping.
//
// Deliberately does not erase-and-retry on a full or version-mismatched NVS
// partition the way nvs_store_init does: that decision belongs in one place,
// and it runs a moment later in wifi_provision::start(). A boot that finds NVS
// unusable falls back to English for that boot and picks the setting up on the
// next one, which is a better trade than two components racing to erase.
ui::Language load_language() {
  if (nvs_flash_init() != ESP_OK) return ui::Language::English;
  nvs_handle_t handle;
  if (nvs_open(kUiNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return ui::Language::English;
  }
  uint8_t stored = 0;
  const esp_err_t found = nvs_get_u8(handle, kLanguageKey, &stored);
  nvs_close(handle);
  // The range check is not paranoia: firmware that shipped more languages
  // could have written a value this build has no row for, and the enum is an
  // index into the string table.
  if (found != ESP_OK ||
      stored >= static_cast<uint8_t>(ui::Language::Count)) {
    return ui::Language::English;
  }
  ESP_LOGI(kTag, "language restored from NVS: %u", stored);
  return static_cast<ui::Language>(stored);
}

// Runs on the LVGL thread, from the settings row that cycles the language. An
// NVS commit is a flash write of a few tens of milliseconds - visible as one
// slow frame on a panel that takes longer than that to refresh anyway, and far
// simpler than handing a one-byte write to its own task.
void store_language(ui::Language value) {
  nvs_handle_t handle;
  if (nvs_open(kUiNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    ESP_LOGW(kTag, "language not saved: NVS unavailable");
    return;
  }
  esp_err_t result = nvs_set_u8(handle, kLanguageKey,
                                static_cast<uint8_t>(value));
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "language not saved: %s", esp_err_to_name(result));
  }
}

// Same namespace as language above (kUiNamespace, "ui_prefs") - one on-device
// preference store, not a second mechanism invented for a second setting.
constexpr char kVolumePresetKey[] = "vol_preset";

// Same reasoning as load_language(): read before the first render (well
// before the first tone can play), and fall back to the compiled-in default
// rather than treating an unreadable NVS as fatal - this board's job is the
// display, not the alarm.
ui::VolumePreset load_volume_preset() {
  if (nvs_flash_init() != ESP_OK) return ui::VolumePreset::Medium;
  nvs_handle_t handle;
  if (nvs_open(kUiNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return ui::VolumePreset::Medium;
  }
  uint8_t stored = 0;
  const esp_err_t found = nvs_get_u8(handle, kVolumePresetKey, &stored);
  nvs_close(handle);
  if (found != ESP_OK ||
      stored >= static_cast<uint8_t>(ui::VolumePreset::Count)) {
    return ui::VolumePreset::Medium;
  }
  ESP_LOGI(kTag, "volume preset restored from NVS: %u", stored);
  return static_cast<ui::VolumePreset>(stored);
}

// Registered as ui::set_volume_preset_store_handler - persistence only, same
// as store_language above, and for the same reason it is a separate handler
// from apply_volume_preset_change below: this one also runs from the silent
// boot-time restore (ui::set_volume_preset(load_volume_preset())), which
// must never make a sound.
void store_volume_preset(ui::VolumePreset value) {
  nvs_handle_t handle;
  if (nvs_open(kUiNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    ESP_LOGW(kTag, "volume preset not saved: NVS unavailable");
    return;
  }
  esp_err_t result = nvs_set_u8(handle, kVolumePresetKey,
                                static_cast<uint8_t>(value));
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "volume preset not saved: %s", esp_err_to_name(result));
  }
}

// Registered as ui::set_volume_changed_handler - runs only when the Volume
// row is actually cycled, never at boot (see that handler's own comment in
// ui_app.hpp for why persistence and hardware application are two separate
// handlers rather than one). Pushes the new preset's percentage into
// modules/audio's own volume - the same audio::audio_set_volume() the
// debug-only `POST /beep?vol=` route calls directly - and plays a short
// confirmation tone so the row's effect is heard immediately, the same way
// a language change is seen immediately.
//
// audio_set_volume() has no concept of "preset" versus "debug override":
// whichever caller runs last simply wins, for the rest of this boot. The
// difference is that only this path (and the silent restore at boot) ever
// writes to NVS, so a reboot always returns to whatever preset is stored
// here, regardless of any `?vol=` used since. This function never reads
// AppSnapshot or app_core - it is entirely local, alarm/notification-tone
// volume, and stays that way; see ui::VolumePreset's own comment for why an
// eventual AirPlay path must not be wired through this at all.
void apply_volume_preset_change() {
  const int percent = ui::volume_preset_percent(ui::volume_preset());
  audio::audio_set_volume(percent);
  // Short: this is a confirmation chirp on a settings row, not an alarm -
  // long enough to be heard as a beep, short enough not to be a nuisance on
  // every cycle through the four presets.
  constexpr int kConfirmFrequencyHz = 2000;
  constexpr int kConfirmDurationMs = 150;
  const esp_err_t result =
      audio::audio_play_tone_async(kConfirmFrequencyHz, kConfirmDurationMs);
  // Not fatal either way - refused only if a tone/sweep is already playing
  // (ESP_ERR_INVALID_STATE) or audio was never initialized
  // (ESP_ERR_NOT_SUPPORTED with CONFIG_AUDIO_ENABLE=n); worth a log line so
  // "I changed the preset and heard nothing" has an answer in either case.
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "volume preset confirmation tone did not play: %s",
             esp_err_to_name(result));
  }
}

// Where the last check left its download URL. Written by the check task and
// read by the LVGL thread when the install row is selected, so it is guarded
// rather than merely assumed to be quiescent between the two.
portMUX_TYPE g_found_lock = portMUX_INITIALIZER_UNLOCKED;
std::string g_found_url;

void set_found_url(const std::string& url) {
  // Copy before the lock and let the old value die after it: a portMUX section
  // runs with interrupts off, and neither malloc nor free belongs in one.
  std::string copy = url;
  taskENTER_CRITICAL(&g_found_lock);
  copy.swap(g_found_url);
  taskEXIT_CRITICAL(&g_found_lock);
}

std::string take_found_url() {
  taskENTER_CRITICAL(&g_found_lock);
  std::string url;
  url.swap(g_found_url);
  taskEXIT_CRITICAL(&g_found_lock);
  return url;
}

void update_check_task(void*) {
  const ota::ReleaseInfo release = ota::check_latest_release();
  ESP_LOGI(kTag, "update check: ok=%d newer=%d version=%s", release.ok,
           release.update_available, release.version.c_str());
  const bool installable =
      release.update_available && !release.firmware_url.empty();
  // Found, not installed. Pulling firmware stays a decision, and making a
  // check silently reflash the device would mean there was no way to ask "is
  // there an update?" without getting one. What changes is only that the
  // answer is now reachable from the board: the row that asked the question
  // becomes the row that acts on it.
  if (installable) {
    set_found_url(release.firmware_url);
    ESP_LOGW(kTag, "update %s available at %s", release.version.c_str(),
             release.firmware_url.c_str());
  }
  // Notes ride along only when there is an actual decision to help with -
  // showing them next to "up to date" or an error answers a question nobody
  // is being asked. release.notes is already ASCII-safe and pre-truncated
  // (see ota_notes.hpp); this is the one and only place they reach the
  // panel, through the same ui::set_update_status() the plain message
  // always used, not through OtaData - see that struct's own comment on why
  // it stays that way.
  std::string status = release.message;
  if (installable && !release.notes.empty()) {
    status += " - " + release.notes;
  }
  ui::set_update_status(status, installable);
  vTaskDelete(nullptr);
}

// Puts the confirm prompt on the panel and takes it away again. Routed through
// wifi_provision like every other snapshot change rather than letting the ota
// component reach into the UI.
void show_update_prompt(bool showing, const std::string& peer,
                        const std::string& version) {
  app_core::OtaData data;
  if (showing) {
    data.phase = app_core::OtaPhase::AwaitingConfirm;
    data.detail = peer;
    data.version = version;
  }
  wifi_provision::set_ota(data);
}

// The settings update row, both halves of it. Called on the LVGL thread, so
// neither branch may block: each hands off to a task and returns.
void run_update_action(bool install) {
  if (install) {
    const std::string url = take_found_url();
    if (url.empty()) {
      // The offer outlived the URL - a reboot, or a second install after the
      // first consumed it. Re-check rather than reflash something stale.
      ui::set_update_status("Check again before installing");
      return;
    }
    // The same downloader POST /ota-url runs, feeding the same ota::Session as
    // a push, so all three routes share one set of header checks, one progress
    // screen and one rollback path.
    if (!ota::start_pull(url)) ui::set_update_status("Device busy");
    return;
  }
  // 16384 B: an HTTPS handshake plus a JSON parse, the same shape as
  // weather_monitor_task, which needed this much for the same reasons.
  if (xTaskCreate(&update_check_task, "ota_check", 16384, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag, "update check task creation failed");
    ui::set_update_status("Device busy");
  }
}

// Readings arrive far faster than a history slot. They are averaged here and
// committed once per slot, because a flash write per reading is the one thing
// that would turn a 240-year wear budget into a handful of years - see the
// arithmetic in history_store.hpp.
portMUX_TYPE g_slot_lock = portMUX_INITIALIZER_UNLOCKED;
int32_t g_battery_sum = 0;
uint16_t g_battery_n = 0;
int32_t g_temperature_sum_decic = 0;
int32_t g_humidity_sum = 0;
uint16_t g_environment_n = 0;

void accumulate_battery(int millivolts) {
  taskENTER_CRITICAL(&g_slot_lock);
  g_battery_sum += millivolts;
  ++g_battery_n;
  taskEXIT_CRITICAL(&g_slot_lock);
}

void accumulate_environment(double temperature_c, uint8_t humidity_percent) {
  taskENTER_CRITICAL(&g_slot_lock);
  g_temperature_sum_decic += static_cast<int32_t>(temperature_c * 10.0);
  g_humidity_sum += humidity_percent;
  ++g_environment_n;
  taskEXIT_CRITICAL(&g_slot_lock);
}

// Averages whatever arrived during the slot and resets the accumulator.
// Sources that produced nothing leave their field marked absent rather than
// contributing a zero.
app_core::HistorySample take_slot() {
  int32_t battery_sum = 0;
  int32_t temperature_sum = 0;
  int32_t humidity_sum = 0;
  uint16_t battery_n = 0;
  uint16_t environment_n = 0;
  taskENTER_CRITICAL(&g_slot_lock);
  battery_sum = g_battery_sum;
  battery_n = g_battery_n;
  temperature_sum = g_temperature_sum_decic;
  humidity_sum = g_humidity_sum;
  environment_n = g_environment_n;
  g_battery_sum = 0;
  g_battery_n = 0;
  g_temperature_sum_decic = 0;
  g_humidity_sum = 0;
  g_environment_n = 0;
  taskEXIT_CRITICAL(&g_slot_lock);

  app_core::HistorySample sample;
  if (battery_n > 0) {
    sample.battery_millivolts =
        static_cast<uint16_t>(battery_sum / battery_n);
  }
  if (environment_n > 0) {
    sample.temperature_decic =
        static_cast<int16_t>(temperature_sum / environment_n);
    sample.humidity_percent =
        static_cast<uint8_t>(humidity_sum / environment_n);
  }
  return sample;
}

[[noreturn]] void history_recorder_task(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(app_core::kHistoryIntervalMinutes * 60'000));
    const app_core::HistorySample sample = take_slot();
    // An entirely empty slot is still recorded. The gap is information - it is
    // how the estimator knows the window it is fitting has holes in it - and
    // skipping it would silently compress the time axis, making an old
    // discharge look like a recent one.
    const esp_err_t result = history_store::record(sample);
    if (result != ESP_OK) {
      ESP_LOGW(kTag, "history slot not persisted: %s",
               esp_err_to_name(result));
      continue;
    }
    const app_core::RuntimeEstimate estimate = app_core::estimate_runtime(
        history_store::current().samples, history_store::current().count,
        app_core::kHistoryIntervalMinutes);
    ESP_LOGI(kTag,
             "history: %u slots, trend=%d %.2f%%/h known=%d minutes=%u",
             static_cast<unsigned>(history_store::current().count),
             static_cast<int>(estimate.trend),
             static_cast<double>(estimate.percent_per_hour), estimate.known,
             static_cast<unsigned>(estimate.minutes_remaining));
    wifi_provision::set_runtime_estimate(estimate);
  }
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

      accumulate_battery(battery.millivolts);
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
// One history point per half hour, so the eight slots span four hours - long
// enough for a room's trend to be a shape rather than noise, short enough that
// the display says something on the day it is switched on. The reading itself
// is still sampled every minute; this only decides how often one is kept.
//
// In RAM, so it starts empty after a reboot. The chart draws only the points
// that exist rather than padding with zeros, which is why the count travels
// with the array.
constexpr uint32_t kIndoorHistoryIntervalMs = 30 * 60'000;

[[noreturn]] void indoor_monitor_task(void*) {
  std::array<double, 8> history{};
  // Seeded from flash rather than starting empty: the chart used to lose
  // everything on every reboot, which on a board that reboots for each
  // firmware push meant it was almost never populated.
  uint8_t history_count = app_core::history_recent_temperatures(
      history_store::current(), history.data(),
      static_cast<uint8_t>(history.size()));
  if (history_count > 0) {
    ESP_LOGI(kTag, "indoor history: seeded %u point(s) from flash",
             history_count);
  }
  uint32_t since_history_ms = kIndoorHistoryIntervalMs;  // record immediately
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
      accumulate_environment(indoor.temperature_c, indoor.humidity_percent);
    } else {
      ESP_LOGW(kTag, "SHTC3 read failed");
    }
    // Only a good reading advances the history; a failed read must not push a
    // gap into the series and it must not silently age the interval either.
    if (indoor.valid) {
      since_history_ms += kIndoorSamplePeriodMs;
      if (since_history_ms >= kIndoorHistoryIntervalMs) {
        since_history_ms = 0;
        if (history_count < history.size()) {
          history[history_count++] = indoor.temperature_c;
        } else {
          for (std::size_t i = 1; i < history.size(); ++i) {
            history[i - 1] = history[i];
          }
          history[history.size() - 1] = indoor.temperature_c;
        }
        ESP_LOGI(kTag, "indoor history: %u/%u points, newest %.1f C",
                 history_count, static_cast<unsigned>(history.size()),
                 indoor.temperature_c);
      }
    }
    indoor.temperature_history_c = history;
    indoor.temperature_history_count = history_count;

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
// A fetch issued before DHCP completes fails with ESP_ERR_HTTP_CONNECT, and
// the provider then sleeps its full refresh interval - which is how a boot race
// turned into half an hour of NO DATA on a network that was already up. Wait
// for the address rather than guessing a startup delay.
void wait_for_station_ip() {
  while (!wifi_provision::station_has_ip()) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// A failed fetch retries sooner than the normal interval so a transient outage
// does not cost a full cycle, but not so often that a rate-limited or broken
// endpoint gets hammered.
constexpr uint32_t kProviderRetryPeriodMs = 5 * 60'000;

[[noreturn]] void weather_monitor_task(void*) {
  wait_for_station_ip();
  for (;;) {
    const bool ok = weather::refresh();
    const app_core::WeatherData current = weather::current();
    // Logged on success as well as failure: a silent success and a silent
    // failure are indistinguishable from a serial capture, and that ambiguity
    // has cost this project several debugging cycles already.
    ESP_LOGI(kTag, "weather refresh ok=%d valid=%d stale=%d temp_c=%.1f", ok,
             current.valid, current.stale, current.current.temperature_c);
    wifi_provision::set_weather(current);
    vTaskDelay(pdMS_TO_TICKS(ok ? weather::kRefreshIntervalSeconds * 1000
                                : kProviderRetryPeriodMs));
  }
}

// Split from a single combined task into two independent ones: Taiwan's
// interval is now market-hours-aware (a few minutes during the regular
// session, the flat interval otherwise - see market_schedule.hpp's
// taiwan_refresh_interval_seconds()) while US stays on the flat interval
// unconditionally. A shared task can only sleep one duration between
// iterations, so keeping them together would have meant either refreshing
// US as often as Taiwan (paying for a cadence nothing asked for) or Taiwan
// only as often as US (the exact staleness this split exists to fix).
//
// refresh_taiwan()/refresh_us() already set their own cache to invalid on
// any total failure (network, bad shape, both sources down for Taiwan)
// rather than leaving a stale or substituted value, so taiwan()/us() are
// safe to publish unconditionally right after each refresh call.
[[noreturn]] void taiwan_market_monitor_task(void*) {
  wait_for_station_ip();
  for (;;) {
    const bool ok = market::refresh_taiwan();
    const app_core::MarketData taiwan = market::taiwan();
    const bool using_primary = market::taiwan_using_primary_source();
    // taiwan.session_elapsed_fraction is already correct here - see its
    // own comment in app_snapshot.hpp: market_parse.cpp's
    // parse_yahoo_quote() computes it directly from Yahoo's own response
    // metadata, not from anything this task needs to derive. An earlier
    // version of this task computed it here instead, from the device's
    // own RTC and market_schedule.hpp's hardcoded session bounds -
    // superseded once the Yahoo-metadata approach turned out to need no
    // clock at all and to cover the US market the RTC-based one could not.

    // source= logged every cycle, not just on a fallback: without it, a
    // silent, permanent Yahoo failure would look identical in the log to a
    // working board that simply has no intraday chart today, and it would
    // go unnoticed for weeks.
    ESP_LOGI(kTag,
             "taiwan refresh ok=%d valid=%d value=%d intraday=%d source=%s "
             "session_fraction=%.2f",
             ok, taiwan.valid, taiwan.primary_value, taiwan.has_intraday,
             using_primary ? "Yahoo" : "TWSE",
             static_cast<double>(taiwan.session_elapsed_fraction));
    wifi_provision::set_taiwan_market(taiwan);

    uint32_t interval_ms;
    if (!ok) {
      // Both sources failed - the same fast retry every other provider
      // uses, not the fallback-only slow interval: a total outage needs to
      // be noticed and re-tried soon, not treated as "fallback is fine".
      interval_ms = kProviderRetryPeriodMs;
    } else {
      // Deciding how soon to poll again is the one thing here that still
      // needs the device's own clock and market_schedule.hpp's session
      // bounds - unlike session_elapsed_fraction above, this has to be
      // answered before the next response exists to read metadata from.
      app_core::RtcDateTime local_time{};
      const bool have_clock = net_time::synced() && net_time::now(local_time);
      // Without a synced clock there is no trustworthy local time to judge
      // market hours by; the flat interval is the same safe default this
      // refresh already used before market-hours awareness existed.
      interval_ms = static_cast<uint32_t>(
                        have_clock ? market::taiwan_refresh_interval_seconds(
                                         local_time, using_primary)
                                   : market::kRefreshIntervalSeconds) *
                    1000;
    }
    vTaskDelay(pdMS_TO_TICKS(interval_ms));
  }
}

// US stays on the flat interval market.hpp defines (30 min) - see that
// header's own comment on kRefreshIntervalSeconds for why: US trading
// hours are a second, DST-observing timezone this component has no access
// to (Taiwan's fixed CST-8/no-DST offset is what makes its market-hours
// check simple enough to do without one), and there has been no reported
// staleness complaint about this page the way there was for Taiwan.
[[noreturn]] void us_market_monitor_task(void*) {
  wait_for_station_ip();
  for (;;) {
    const bool ok = market::refresh_us();
    const app_core::MarketData us = market::us();
    ESP_LOGI(kTag, "us refresh ok=%d valid=%d value=%d intraday=%d", ok,
             us.valid, us.primary_value, us.has_intraday);
    wifi_provision::set_us_market(us);
    vTaskDelay(pdMS_TO_TICKS(ok ? market::kRefreshIntervalSeconds * 1000
                                : kProviderRetryPeriodMs));
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
  // Once on the first successful sync, then daily. The point is not to keep
  // the RTC in step second by second - it is to leave a trustworthy time in
  // the chip so a boot with no network still knows what time it is. Daily
  // re-writes keep the crystal's drift from accumulating across the months a
  // device like this stays powered.
  bool rtc_written = false;
  uint32_t since_rtc_write_ms = 0;
  constexpr uint32_t kRtcRefreshMs = 24 * 60 * 60 * 1000;
  for (;;) {
    app_core::RtcDateTime clock{};
    if (net_time::synced() && net_time::now(clock)) {
      app_core::ClockData data;
      format_clock(clock, data);
      wifi_provision::set_clock(data);

      if (!rtc_written || since_rtc_write_ms >= kRtcRefreshMs) {
        if (write_rtc(clock)) {
          rtc_written = true;
          since_rtc_write_ms = 0;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kNetTimeCheckPeriodMs));
    since_rtc_write_ms += kNetTimeCheckPeriodMs;
  }
}

// One-shot, not [[noreturn]] like the monitor tasks above: net_log::start()
// installs its own log sink and sender task internally, so once that call
// returns this task's job is done and it deletes itself rather than
// looping forever for no reason.
void net_log_startup_task(void*) {
  wait_for_station_ip();
  const esp_err_t result = net_log::start();
  if (result == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGI(kTag, "net_log disabled (set CONFIG_NET_LOG_ENABLE=y to enable)");
  } else if (result != ESP_OK) {
    // Non-fatal: serial logging is unaffected either way.
    ESP_LOGE(kTag, "net_log startup failed: %s", esp_err_to_name(result));
  }
  vTaskDelete(nullptr);
}

}  // namespace

extern "C" void app_main() {
  const size_t psram_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  ESP_LOGI(kTag, "startup diagnostics PSRAM bytes=%u",
           static_cast<unsigned>(psram_bytes));
  if (psram_bytes == 0) fatal_loop("required PSRAM unavailable", ESP_ERR_NOT_FOUND);

  // Retain the log from here rather than from when the socket opens. Every
  // startup decision below - the language restored from NVS, the history
  // restored from flash, the rollback guard's reading of the slot - is logged
  // in the next few seconds, and net_log's listener cannot exist until lwIP
  // does. Without this the answers were gone before anyone could connect,
  // which on a board with no cable means they were unobservable.
  (void)net_log::begin();

  // Why the last boot ended, logged where the network can see it.
  //
  // A panic writes its backtrace straight to UART and never reaches the log
  // sink, and the reboot takes the retained ring with it, so on a board with
  // no cable a crash leaves no trace at all - the only symptom is an image
  // that quietly gets rolled back. This is the one breadcrumb that survives,
  // because the reason is held in RTC memory across the reset.
  const esp_reset_reason_t reset_reason = esp_reset_reason();
  const char* reset_text = "other";
  switch (reset_reason) {
    case ESP_RST_POWERON: reset_text = "power-on"; break;
    case ESP_RST_SW: reset_text = "software restart"; break;
    case ESP_RST_PANIC: reset_text = "PANIC (crash)"; break;
    case ESP_RST_INT_WDT: reset_text = "interrupt watchdog"; break;
    case ESP_RST_TASK_WDT: reset_text = "task watchdog"; break;
    case ESP_RST_WDT: reset_text = "other watchdog"; break;
    case ESP_RST_BROWNOUT: reset_text = "brownout"; break;
    case ESP_RST_EXT: reset_text = "external reset"; break;
    default: break;
  }
  if (reset_reason == ESP_RST_PANIC || reset_reason == ESP_RST_INT_WDT ||
      reset_reason == ESP_RST_TASK_WDT || reset_reason == ESP_RST_WDT ||
      reset_reason == ESP_RST_BROWNOUT) {
    ESP_LOGE(kTag, "previous boot ended in %s (reason %d)", reset_text,
             static_cast<int>(reset_reason));
  } else {
    ESP_LOGI(kTag, "previous boot ended in %s (reason %d)", reset_text,
             static_cast<int>(reset_reason));
  }

  esp_err_t result = board::display_init();
  if (result != ESP_OK) fatal_loop("display initialization failed", result);
  ESP_LOGI(kTag, "startup diagnostics display=ready");

  result = board::lvgl_init();
  if (result != ESP_OK) fatal_loop("LVGL initialization failed", result);
  ESP_LOGI(kTag, "startup diagnostics LVGL=ready");

  app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  // Everything on this board that is not the display hangs off one I2C bus,
  // including the ES7210 mic ADC nothing here drives yet. Logging what
  // actually answers costs one line per boot and settles "is the part
  // fitted" without a cable or a multimeter.
  if (board::board_i2c_init() == ESP_OK) board::board_i2c_scan();

  // Never fatal: this board's primary job is the display, and a codec that
  // is absent or unresponsive should mean no sound, not no boot. Nothing
  // plays here - audio_init() only readies the I2S/codec path, and leaves
  // the amplifier off until a /beep request asks for a tone.
  //
  // A disabled module logging a warning every boot teaches people to skim
  // past real warnings, so "compiled out" and "compiled in but failed" get
  // told apart here. audio's stub and es8311/i2s's real failures can return
  // the exact same code (esp_codec_dev's ESP_CODEC_DEV_NOT_SUPPORT is
  // literally ESP_ERR_NOT_SUPPORTED, and the i2s driver returns it for real
  // config failures too), so the return value can't be trusted to tell them
  // apart - unlike airplay below, this one case needs the #ifdef.
  const esp_err_t audio_result = audio::audio_init();
#ifdef CONFIG_AUDIO_ENABLE
  if (audio_result == ESP_OK) {
    ESP_LOGI(kTag, "startup diagnostics audio=ready (ES8311)");
  } else {
    ESP_LOGW(kTag, "startup diagnostics audio=unavailable: %s",
             esp_err_to_name(audio_result));
  }
#else
  ESP_LOGI(kTag, "startup diagnostics audio=disabled (CONFIG_AUDIO_ENABLE=n)");
#endif

  // Same non-fatal treatment as audio_init() just above, for the same
  // reason: this board's primary job is the display, and a real AirPlay
  // stack that fails to start must mean no AirPlay, not no boot. No #ifdef
  // around the call itself - airplay.hpp's inline no-ops make it correct
  // either way. Unlike audio just above, the disabled-vs-failed split here
  // can lean on the return code instead of a second #ifdef:
  // ESP_ERR_NOT_SUPPORTED is airplay_init()'s stub signature and only its
  // stub signature - the real path's raop_init() returns ESP_OK or one of
  // ESP_ERR_RAOP_* (0x7000+, esp_raop_receiver.h), never
  // ESP_ERR_NOT_SUPPORTED, so this can't misclassify a genuine startup
  // failure as "not compiled in".
  const esp_err_t airplay_result = airplay::airplay_init();
  if (airplay_result == ESP_OK) {
    ESP_LOGI(kTag, "startup diagnostics airplay=ready");
  } else if (airplay_result == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGI(kTag,
             "startup diagnostics airplay=disabled (CONFIG_AIRPLAY_ENABLE=n)");
  } else {
    ESP_LOGW(kTag, "startup diagnostics airplay=unavailable: %s",
             esp_err_to_name(airplay_result));
  }

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

  // Before start(), so the first frame is already in the right language.
  // The store handler is registered after the load on purpose: set_language
  // only notifies on an actual change, and registering first would have the
  // restore write straight back what it just read.
  // Before ui::start(), so the sensor chart's first render already carries
  // the history from previous boots instead of drawing an empty box and
  // filling in over the next hour.
  if (history_store::init() != ESP_OK) {
    ESP_LOGW(kTag, "history storage unavailable; charts start empty");
  }

  ui::set_language(load_language());
  ui::set_language_store_handler(&store_language);

  // Same silent-restore reasoning as language above: set_volume_preset()
  // only calls its store handler on an actual change, which is exactly why
  // the restored percentage still has to be pushed into modules/audio
  // explicitly and directly here - no confirmation tone, this is a boot,
  // not a settings-row press. set_volume_changed_handler (the audible,
  // interactive path) is registered further down with the rest of this
  // board's cross-module wiring, not here, so it cannot fire yet.
  ui::set_volume_preset(load_volume_preset());
  ui::set_volume_preset_store_handler(&store_volume_preset);
  audio::audio_set_volume(ui::volume_preset_percent(ui::volume_preset()));

  if (!ui::start(snapshot, clock, !rtc_ok)) {
    fatal_loop("UI lifecycle initialization failed", ESP_FAIL);
  }
  ESP_LOGI(kTag, "startup diagnostics registry=ready cycle=1");

  // Same indirection as the setup gesture below: wifi_provision owns the one
  // AppSnapshot, so the ota component is handed a way to publish rather than
  // depending on it and inverting the layering.
  ota::set_progress_handler(&wifi_provision::set_ota);
  ui::set_setup_gesture_handler(&wifi_provision::toggle_setup);
  ui::set_update_handler(&run_update_action);
  ui::set_volume_changed_handler(&apply_volume_preset_change);
  // Lets GET /shot answer with what is on the panel right now. No-op in a
  // release build, where the route does not exist.
  wifi_provision::set_screenshot_provider(&board::framebuffer_snapshot);
  ota::set_confirm_prompt_handler(&show_update_prompt);
  // No wiring call here for audio's tray indicator (there used to be one):
  // audio_init() above already registered it directly with app_core's tray
  // registry, which needs no handler indirection at all - see
  // tray_registry.hpp.
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

  // Started after wifi_provision (it publishes through it) and before the
  // provider tasks, so a pending image is judged on the render loop alone
  // rather than on whether the network happened to come up in time.
  // 4096 B: partition-table reads, two counter samples and a log line.
  if (xTaskCreate(&ota_guard_task, "ota_guard", 4096, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    // Non-fatal, but worth shouting about: without this task a pending image
    // is never marked valid, so the next reset silently rolls it back.
    ESP_LOGE(kTag, "ota guard task creation failed; a pending image will not "
                   "be confirmed");
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

  // 8192 B each: market::http_get() heap-allocates its response body
  // (std::string), so unlike weather this task's stack only has to cover
  // the TLS handshake and JSON parsing depth for a request or two, not a
  // large local buffer. Two tasks, not one, now that Taiwan and US refresh
  // on genuinely different cadences - see taiwan_market_monitor_task's own
  // comment for why a shared task could not do that.
  if (xTaskCreate(&taiwan_market_monitor_task, "taiwan_market_monitor", 8192,
                  nullptr, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag, "taiwan market monitor task creation failed");
  }
  if (xTaskCreate(&us_market_monitor_task, "us_market_monitor", 8192, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag, "us market monitor task creation failed");
  }

  // 4096 B: waits, then makes a handful of esp_netif/socket/task-creation
  // calls and deletes itself - no TLS, no JSON, no large buffers.
  if (xTaskCreate(&net_log_startup_task, "net_log_startup", 4096, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag, "net_log startup task creation failed");
  }

  // 4096 B: averages a handful of integers and hands a 3.5 KiB static buffer
  // to esp_partition. Nothing of its own goes on the stack.
  if (xTaskCreate(&history_recorder_task, "history_rec", 4096, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag, "history recorder task creation failed");
  }
}
