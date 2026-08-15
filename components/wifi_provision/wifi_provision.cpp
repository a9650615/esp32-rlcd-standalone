#include "wifi_provision.hpp"

#include "internal.hpp"
#include "ui_app.hpp"

#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdio>
#include <optional>

namespace wifi_provision {
namespace {

constexpr char kTag[] = "wifi_provision";
constexpr char kPortalUrl[] = "http://192.168.4.1/";

app_core::AppSnapshot snapshot_;
std::optional<wifi_config::StateMachine> state_machine_;
std::string ap_ssid_;
// Regenerated fresh on every entry into SetupAp; never written to NVS.
std::string ap_password_;
// Set only when retries are exhausted and SetupAp is (re-)entered because of
// a real failure; cleared on every other transition. Drives both the status
// text and SetupData::error.
DisconnectReason last_disconnect_reason_ = DisconnectReason::None;
bool portal_active_ = false;
SemaphoreHandle_t mutex_ = nullptr;

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

// Starts/stops the AP + portal to match the current state and republishes
// the snapshot. Mutex must be held by the caller.
void apply_state_and_publish() {
  using State = wifi_config::StateMachine::State;
  const State state = state_machine_->state();

  if (state == State::SetupAp) {
    if (!portal_active_) {
      if (ap_ssid_.empty()) {
        ap_ssid_ = wifi_config::setup_ap_ssid(wifi_manager_ap_mac());
      }
      uint8_t random_bytes[wifi_config::kPassphraseLength];
      esp_fill_random(random_bytes, sizeof(random_bytes));
      ap_password_ =
          wifi_config::format_passphrase(random_bytes, sizeof(random_bytes));
      wifi_manager_start_ap(ap_ssid_, ap_password_);
      portal_start();
      portal_active_ = true;
    }
  } else if (state == State::Connected) {
    if (portal_active_) {
      portal_stop();
      wifi_manager_stop_ap();
      portal_active_ = false;
    }
    ap_password_.clear();
  }

  snapshot_.setup.active = (state == State::SetupAp);
  snapshot_.setup.connected = (state == State::Connected);
  snapshot_.setup.ap_ssid = ap_ssid_;
  snapshot_.setup.ap_password = ap_password_;
  snapshot_.setup.portal_url = ap_ssid_.empty() ? std::string{} : kPortalUrl;
  snapshot_.setup.qr_payload =
      ap_ssid_.empty() ? std::string{}
                        : wifi_config::wifi_qr_payload(ap_ssid_, ap_password_);
  snapshot_.setup.status = status_for_state();
  snapshot_.setup.error = error_for_state();
  ui::publish_snapshot(snapshot_);
}

}  // namespace

esp_err_t start(const app_core::AppSnapshot& snapshot) {
  snapshot_ = snapshot;
  mutex_ = xSemaphoreCreateMutex();
  if (mutex_ == nullptr) return ESP_ERR_NO_MEM;

  esp_err_t result = nvs_store_init();
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

void set_battery(const app_core::BatteryData& battery) {
  lock();
  snapshot_.battery = battery;
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
