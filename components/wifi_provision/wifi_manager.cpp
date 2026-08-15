#include "internal.hpp"

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>

#include <algorithm>
#include <cstring>

namespace wifi_provision {
namespace {

bool wifi_started_ = false;
bool ap_active_ = false;
bool sta_configured_ = false;

DisconnectReason classify_reason(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return DisconnectReason::AuthFailure;
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
      return DisconnectReason::NotFound;
    default:
      return DisconnectReason::Other;
  }
}

// Wi-Fi must have its interface config set before the first esp_wifi_start()
// or it can briefly come up with an empty/default SSID.
void ensure_started() {
  if (!wifi_started_) {
    esp_wifi_start();
    wifi_started_ = true;
  }
}

void wifi_event_handler(void*, esp_event_base_t, int32_t event_id,
                        void* event_data) {
  if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    const auto* event =
        static_cast<wifi_event_sta_disconnected_t*>(event_data);
    handle_wifi_disconnected(classify_reason(event->reason));
  }
}

void ip_event_handler(void*, esp_event_base_t, int32_t event_id, void*) {
  if (event_id == IP_EVENT_STA_GOT_IP) handle_wifi_connected();
}

}  // namespace

esp_err_t wifi_manager_init() {
  esp_err_t result = esp_netif_init();
  if (result != ESP_OK) return result;
  result = esp_event_loop_create_default();
  if (result != ESP_OK) return result;
  if (esp_netif_create_default_wifi_sta() == nullptr) return ESP_FAIL;
  if (esp_netif_create_default_wifi_ap() == nullptr) return ESP_FAIL;

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  result = esp_wifi_init(&cfg);
  if (result != ESP_OK) return result;

  result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      &wifi_event_handler, nullptr);
  if (result != ESP_OK) return result;
  return esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                    &ip_event_handler, nullptr);
}

void wifi_manager_start_ap(const std::string& ssid, const std::string& password) {
  wifi_config_t ap_cfg{};
  const size_t len = std::min(ssid.size(), sizeof(ap_cfg.ap.ssid));
  std::memcpy(ap_cfg.ap.ssid, ssid.data(), len);
  ap_cfg.ap.ssid_len = static_cast<uint8_t>(len);
  const size_t pass_len =
      std::min(password.size(), sizeof(ap_cfg.ap.password));
  std::memcpy(ap_cfg.ap.password, password.data(), pass_len);
  ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ap_cfg.ap.max_connection = 4;
  ap_cfg.ap.channel = 1;

  ap_active_ = true;
  esp_wifi_set_mode(sta_configured_ ? WIFI_MODE_APSTA : WIFI_MODE_AP);
  esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
  ensure_started();
}

void wifi_manager_stop_ap() {
  ap_active_ = false;
  // Wi-Fi is already started by the time an AP is ever torn down.
  esp_wifi_set_mode(WIFI_MODE_STA);
}

void wifi_manager_connect_sta(const wifi_config::Credentials& creds) {
  wifi_config_t sta_cfg{};
  const size_t ssid_len =
      std::min(creds.ssid.size(), sizeof(sta_cfg.sta.ssid));
  std::memcpy(sta_cfg.sta.ssid, creds.ssid.data(), ssid_len);
  const size_t pass_len =
      std::min(creds.password.size(), sizeof(sta_cfg.sta.password));
  std::memcpy(sta_cfg.sta.password, creds.password.data(), pass_len);

  sta_configured_ = true;
  esp_wifi_set_mode(ap_active_ ? WIFI_MODE_APSTA : WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  ensure_started();
  esp_wifi_connect();
}

void wifi_manager_reconnect_sta() { esp_wifi_connect(); }

std::array<uint8_t, 6> wifi_manager_ap_mac() {
  std::array<uint8_t, 6> mac{};
  esp_wifi_get_mac(WIFI_IF_AP, mac.data());
  return mac;
}

}  // namespace wifi_provision
