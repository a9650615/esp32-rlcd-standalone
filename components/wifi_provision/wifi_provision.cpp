#include "wifi_provision.hpp"

#include "internal.hpp"
#include "ui_app.hpp"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdio>
#include <optional>

namespace wifi_provision {
namespace {

constexpr char kTag[] = "wifi_provision";
constexpr char kPortalUrl[] = "http://192.168.4.1/";

// Bounds how long "Connecting" can sit without any Wi-Fi event before it's
// treated as a failed attempt. Association plus a DHCP lease normally
// finishes in a couple of seconds; 15s gives a slow/congested AP headroom
// without letting a silent hang (no event ever arriving - the exact way this
// bug was originally triggered) stall the 5-retry budget forever.
constexpr uint64_t kConnectTimeoutUs = 15 * 1000 * 1000ULL;

app_core::AppSnapshot snapshot_;
std::optional<wifi_config::StateMachine> state_machine_;
std::string ap_ssid_;
// Regenerated fresh on every entry into SetupAp; never written to NVS or
// logged. Gates the setup HTTP portal - the AP itself is open.
std::string portal_password_;
// Set only when retries are exhausted and SetupAp is (re-)entered because of
// a real failure; cleared on every other transition. Drives both the status
// text and SetupData::error.
DisconnectReason last_disconnect_reason_ = DisconnectReason::None;
bool portal_active_ = false;
SemaphoreHandle_t mutex_ = nullptr;
esp_timer_handle_t connect_timeout_timer_ = nullptr;

void lock() { xSemaphoreTake(mutex_, portMAX_DELAY); }
void unlock() { xSemaphoreGive(mutex_); }

const char* state_name(wifi_config::StateMachine::State state) {
  using State = wifi_config::StateMachine::State;
  switch (state) {
    case State::Connecting: return "Connecting";
    case State::Connected: return "Connected";
    case State::SetupAp: return "SetupAp";
  }
  return "?";
}

// Mutex must be held by the caller. Logs every call site so a serial
// capture shows the full state trail, including retry-count-only changes
// that don't cross a state boundary (e.g. a mid-budget reconnect attempt).
void log_transition(const char* trigger,
                    wifi_config::StateMachine::State old_state) {
  ESP_LOGI(kTag, "%s: state %s -> %s (retry %d/%d)", trigger,
          state_name(old_state), state_name(state_machine_->state()),
          state_machine_->retries(), wifi_config::StateMachine::kMaxRetries);
}

std::string status_for_state() {
  using State = wifi_config::StateMachine::State;
  switch (state_machine_->state()) {
    case State::Connecting: {
      char buffer[40];
      std::snprintf(buffer, sizeof(buffer), "Connecting - attempt %d of %d",
                    state_machine_->retries() + 1,
                    wifi_config::StateMachine::kMaxRetries);
      return buffer;
    }
    case State::Connected:
      return "Connected";
    case State::SetupAp:
      switch (last_disconnect_reason_) {
        case DisconnectReason::AuthFailure:
          return "Wrong password - try again";
        case DisconnectReason::NotFound:
          return "Network not found - out of range";
        case DisconnectReason::Timeout:
          return "Connection timed out - try again";
        case DisconnectReason::Other:
          return "Could not connect - try again";
        case DisconnectReason::None:
          return "Waiting for phone";
      }
  }
  return {};
}

// True only for a genuine failure state, per SetupData::error's contract.
bool error_for_state() {
  return state_machine_->state() == wifi_config::StateMachine::State::SetupAp &&
        last_disconnect_reason_ != DisconnectReason::None;
}

// esp_timer callback (runs on the esp_timer task, not the LVGL thread) for a
// Connecting attempt that never produced a wifi/ip event. Routes through the
// same handle_wifi_disconnected() path as a real disconnect so the existing
// retry bound and SetupAp fallback apply unchanged; DisconnectReason::Timeout
// keeps it distinguishable from an actual 802.11 disconnect in the log and
// status text.
void connect_timeout_cb(void*) {
  ESP_LOGW(kTag, "connect attempt timed out with no wifi event");
  handle_wifi_disconnected(DisconnectReason::Timeout);
}

// Starts/stops the AP + portal to match the current state and republishes
// the snapshot. Mutex must be held by the caller.
void apply_state_and_publish() {
  using State = wifi_config::StateMachine::State;
  const State state = state_machine_->state();

  if (state == State::Connecting) {
    // (Re-)arm the bound on this attempt; stop first since starting an
    // already-running one-shot timer is an error. Any transition out of
    // Connecting (below) stops it instead.
    esp_timer_stop(connect_timeout_timer_);
    const esp_err_t err =
        esp_timer_start_once(connect_timeout_timer_, kConnectTimeoutUs);
    if (err != ESP_OK) {
      ESP_LOGW(kTag, "esp_timer_start_once failed: %s", esp_err_to_name(err));
    }
  } else {
    esp_timer_stop(connect_timeout_timer_);
  }

  if (state == State::SetupAp) {
    if (!portal_active_) {
      if (ap_ssid_.empty()) {
        ap_ssid_ = wifi_config::setup_ap_ssid(wifi_manager_ap_mac());
      }
      uint8_t random_bytes[wifi_config::kPassphraseLength];
      esp_fill_random(random_bytes, sizeof(random_bytes));
      portal_password_ =
          wifi_config::format_passphrase(random_bytes, sizeof(random_bytes));
      wifi_manager_start_ap(ap_ssid_);
      portal_start();
      portal_active_ = true;
    }
  } else if (state == State::Connected) {
    if (portal_active_) {
      // The AP goes away; the HTTP server stays. It is what a firmware push
      // from a machine on the same network arrives at, and leaving it up is
      // what makes that need no button to enable.
      //
      // Clearing the password is what keeps the setup form shut: with none
      // set, portal_password_ok() refuses everything, so the only route that
      // still answers is the upload - and that one asks the board.
      wifi_manager_stop_ap();
    } else {
      portal_start();
      portal_active_ = true;
    }
    portal_password_.clear();
  }

  snapshot_.setup.active = (state == State::SetupAp);
  snapshot_.setup.connected = (state == State::Connected);
  snapshot_.setup.ap_ssid = ap_ssid_;
  snapshot_.setup.portal_password = portal_password_;
  snapshot_.setup.portal_url = ap_ssid_.empty() ? std::string{} : kPortalUrl;
  // The password rides in the query string, so it lands in the phone's
  // browser history and any on-path log for the LAN hop between the phone
  // and the board. Acceptable here only because it is per-session, never
  // persisted, and dead the moment setup mode exits; the page password (not
  // network isolation - the AP is open and the portal is LAN-reachable by
  // design) is the only thing protecting the portal.
  snapshot_.setup.qr_payload =
      ap_ssid_.empty()
          ? std::string{}
          : wifi_config::portal_qr_payload(kPortalUrl, portal_password_);
  snapshot_.setup.status = status_for_state();
  snapshot_.setup.error = error_for_state();
  ui::publish_snapshot(snapshot_);
}

}  // namespace

esp_err_t start(const app_core::AppSnapshot& snapshot) {
  snapshot_ = snapshot;
  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) return ESP_ERR_NO_MEM;

  esp_timer_create_args_t timer_args{};
  timer_args.callback = &connect_timeout_cb;
  timer_args.dispatch_method = ESP_TIMER_TASK;
  timer_args.name = "wifi_connect_timeout";
  esp_err_t result = esp_timer_create(&timer_args, &connect_timeout_timer_);
  if (result != ESP_OK) return result;

  result = nvs_store_init();
  if (result != ESP_OK) return result;

  wifi_config::Credentials creds;
  const bool has_creds = nvs_load(creds);
  state_machine_.emplace(has_creds);
  ESP_LOGI(kTag, "start: has_creds=%d initial_state=%s", has_creds,
          state_name(state_machine_->state()));

  result = wifi_manager_init();
  if (result != ESP_OK) return result;

  lock();
  if (has_creds) wifi_manager_connect_sta(creds);
  apply_state_and_publish();
  unlock();
  return ESP_OK;
}

void toggle_setup() {
  lock();
  const auto old_state = state_machine_->state();
  last_disconnect_reason_ = DisconnectReason::None;
  state_machine_->on_setup_gesture();
  log_transition("setup_gesture", old_state);
  if (state_machine_->state() == wifi_config::StateMachine::State::Connecting) {
    // Toggled out of setup with saved credentials: reconnect.
    wifi_manager_reconnect_sta();
  }
  apply_state_and_publish();
  unlock();
}

void handle_wifi_connected() {
  lock();
  const auto old_state = state_machine_->state();
  last_disconnect_reason_ = DisconnectReason::None;
  state_machine_->on_connected();
  log_transition("connected", old_state);
  apply_state_and_publish();
  unlock();
}

void handle_wifi_disconnected(DisconnectReason reason) {
  lock();
  const auto old_state = state_machine_->state();
  state_machine_->on_disconnected();
  log_transition("disconnected", old_state);
  if (state_machine_->state() == wifi_config::StateMachine::State::Connecting) {
    wifi_manager_reconnect_sta();
  } else if (state_machine_->state() ==
             wifi_config::StateMachine::State::SetupAp) {
    // Retry budget just got exhausted; surface why.
    last_disconnect_reason_ = reason;
    ESP_LOGW(kTag, "retry budget exhausted, entering setup AP: %s",
            to_string(reason));
  }
  apply_state_and_publish();
  unlock();
}

// Provider tasks start about 1.7 s before DHCP completes, so a fetch issued at
// task start hits a stack with no route and fails with ESP_ERR_HTTP_CONNECT.
// They wait on this instead of guessing a startup delay.
bool station_has_ip() {
  lock();
  const bool connected = snapshot_.setup.connected;
  unlock();
  return connected;
}

void set_battery(const app_core::BatteryData& battery) {
  lock();
  snapshot_.battery = battery;
  ui::publish_snapshot(snapshot_);
  unlock();
}

void set_ota(const app_core::OtaData& ota) {
  lock();
  snapshot_.ota = ota;
  ui::publish_snapshot(snapshot_);
  unlock();
}

void set_indoor(const app_core::IndoorData& indoor) {
  lock();
  snapshot_.indoor = indoor;
  ui::publish_snapshot(snapshot_);
  unlock();
}

void set_runtime_estimate(const app_core::RuntimeEstimate& estimate) {
  lock();
  // Its own AppSnapshot field, not part of BatteryData - see that field's
  // own comment in app_snapshot.hpp for why: this runs on a different task,
  // at a different cadence, than set_battery() below, and the two must not
  // share a struct either one of them assigns to wholesale.
  snapshot_.battery_runtime = estimate;
  ui::publish_snapshot(snapshot_);
  unlock();
}

void set_weather(const app_core::WeatherData& weather) {
  lock();
  snapshot_.weather = weather;
  ui::publish_snapshot(snapshot_);
  unlock();
}

void set_taiwan_market(const app_core::MarketData& market) {
  lock();
  snapshot_.taiwan_market = market;
  ui::publish_snapshot(snapshot_);
  unlock();
}

void set_us_market(const app_core::MarketData& market) {
  lock();
  snapshot_.us_market = market;
  ui::publish_snapshot(snapshot_);
  unlock();
}

void set_clock(const app_core::ClockData& clock) {
  lock();
  snapshot_.clock = clock;
  ui::publish_snapshot(snapshot_);
  unlock();
}

std::string current_ap_ssid() {
  lock();
  std::string result = ap_ssid_;
  unlock();
  return result;
}

std::string current_status_text() {
  lock();
  std::string result = status_for_state();
  unlock();
  return result;
}

std::string current_portal_password() {
  lock();
  std::string result = portal_password_;
  unlock();
  return result;
}

void handle_credentials_saved(const wifi_config::Credentials& creds) {
  lock();
  esp_err_t result = nvs_save(creds);
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "NVS save failed: %s", esp_err_to_name(result));
  }
  const auto old_state = state_machine_->state();
  last_disconnect_reason_ = DisconnectReason::None;
  state_machine_->on_credentials_saved();
  log_transition("credentials_saved", old_state);
  wifi_manager_connect_sta(creds);
  apply_state_and_publish();
  unlock();
}

}  // namespace wifi_provision
