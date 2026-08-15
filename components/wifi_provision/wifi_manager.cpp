#include "internal.hpp"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>

#include <algorithm>
#include <cstring>

namespace wifi_provision {
namespace {

constexpr char kTag[] = "wifi_manager";

bool wifi_started_ = false;
bool ap_active_ = false;
bool sta_configured_ = false;
// True once WIFI_EVENT_STA_CONNECTED has fired and until the next
// WIFI_EVENT_STA_DISCONNECTED. esp_wifi_connect() just returns
// ESP_ERR_WIFI_CONN while this is true (see esp_wifi.h's attention note on
// esp_wifi_connect()) - that silent-failure path is what produced the
// original hang, so callers must check this before connecting.
bool sta_associated_ = false;
// Set right before a deliberate esp_wifi_disconnect() issued to apply a
// config change (new credentials, or a plain retry) while still associated.
// The resulting WIFI_EVENT_STA_DISCONNECTED is expected and must not be
// mistaken for a failed connection attempt - it doesn't reach
// handle_wifi_disconnected()/the retry counter, unlike a real disconnect.
bool pending_reconfig_disconnect_ = false;
// Only kept for the stop-AP log line below; the SSID itself lives in
// wifi_provision.cpp.
std::string ap_ssid_for_log_;

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
  if (wifi_started_) return;
  const esp_err_t err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "esp_wifi_start failed: %s", esp_err_to_name(err));
    return;
  }
  wifi_started_ = true;
}

// Connects using whatever STA config is currently set (wifi_manager_connect_sta()
// has already applied it by the time this runs). esp_wifi_connect() just
// fails silently while still associated to an AP (root cause of the original
// hang), so tear down first when needed; the actual esp_wifi_connect() then
// happens from wifi_event_handler() once the resulting
// WIFI_EVENT_STA_DISCONNECTED arrives, per the ordering ESP-IDF's own
// wifi_provisioning manager uses (set config, then disconnect, then let the
// disconnect handler reconnect - see wifi_prov_mgr_deinit() in
// components/wifi_provisioning/src/manager.c) rather than disconnecting and
// connecting back-to-back, which would race the still-in-flight teardown.
void connect_or_defer_until_disconnected() {
  if (sta_associated_) {
    pending_reconfig_disconnect_ = true;
    const esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
      ESP_LOGW(kTag, "esp_wifi_disconnect failed: %s", esp_err_to_name(err));
    }
    return;
  }
  const esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "esp_wifi_connect failed: %s", esp_err_to_name(err));
  }
}

void wifi_event_handler(void*, esp_event_base_t, int32_t event_id,
                        void* event_data) {
  if (event_id == WIFI_EVENT_STA_CONNECTED) {
    sta_associated_ = true;
    return;
  }
  if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    sta_associated_ = false;
    if (pending_reconfig_disconnect_) {
      // Deliberate teardown from connect_or_defer_until_disconnected(), not
      // a failed attempt - skip handle_wifi_disconnected() entirely so it
      // never touches the retry counter, then connect for real now that the
      // old association is gone.
      pending_reconfig_disconnect_ = false;
      ESP_LOGI(kTag, "sta disconnected (reconfig teardown), connecting with current config");
      const esp_err_t err = esp_wifi_connect();
      if (err != ESP_OK) {
        ESP_LOGW(kTag, "esp_wifi_connect after reconfig failed: %s", esp_err_to_name(err));
      }
      return;
    }
    const auto* event =
        static_cast<wifi_event_sta_disconnected_t*>(event_data);
    const DisconnectReason reason = classify_reason(event->reason);
    // Raw code is what makes the diagnosis defensible; classify_reason()
    // alone can't be checked against the ESP-IDF reason table after the
    // fact.
    ESP_LOGW(kTag, "sta disconnected: raw_reason=%u classified=%s",
             event->reason, to_string(reason));
    handle_wifi_disconnected(reason);
  }
}

void ip_event_handler(void*, esp_event_base_t, int32_t event_id,
                      void* event_data) {
  if (event_id == IP_EVENT_STA_GOT_IP) {
    const auto* event = static_cast<ip_event_got_ip_t*>(event_data);
    ESP_LOGI(kTag, "sta got ip: " IPSTR, IP2STR(&event->ip_info.ip));
    handle_wifi_connected();
  }
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

void wifi_manager_start_ap(const std::string& ssid) {
  wifi_config_t ap_cfg{};
  const size_t len = std::min(ssid.size(), sizeof(ap_cfg.ap.ssid));
  std::memcpy(ap_cfg.ap.ssid, ssid.data(), len);
  ap_cfg.ap.ssid_len = static_cast<uint8_t>(len);
  ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
  ap_cfg.ap.max_connection = 4;
  ap_cfg.ap.channel = 1;

  // Open on purpose: WPA2 here only ever guarded the AP hop, and the setup
  // HTTP server is reachable from the whole LAN regardless (APSTA binds
  // esp_http_server to all interfaces). The page password gates the portal
  // instead - see current_portal_password() in wifi_provision.cpp.
  ESP_LOGI(kTag, "setup AP starting (open): ssid=%s", ssid.c_str());
  ap_ssid_for_log_ = ssid;
  ap_active_ = true;
  esp_err_t err = esp_wifi_set_mode(sta_configured_ ? WIFI_MODE_APSTA : WIFI_MODE_AP);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "esp_wifi_set_mode(AP) failed: %s", esp_err_to_name(err));
  }
  err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "esp_wifi_set_config(AP) failed: %s", esp_err_to_name(err));
  }
  ensure_started();
}

void wifi_manager_stop_ap() {
  ESP_LOGI(kTag, "setup AP stopping: ssid=%s", ap_ssid_for_log_.c_str());
  ap_active_ = false;
  // Wi-Fi is already started by the time an AP is ever torn down.
  const esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "esp_wifi_set_mode(STA) failed: %s", esp_err_to_name(err));
  }
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
  esp_err_t err = esp_wifi_set_mode(ap_active_ ? WIFI_MODE_APSTA : WIFI_MODE_STA);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "esp_wifi_set_mode(STA) failed: %s", esp_err_to_name(err));
  }
  // Config must be applied before any disconnect below, not after: if we're
  // still associated to the old AP, connect_or_defer_until_disconnected()
  // defers the actual esp_wifi_connect() to wifi_event_handler(), which only
  // knows whatever config is set at that later point in time.
  err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "esp_wifi_set_config(STA) failed: %s", esp_err_to_name(err));
  }
  ensure_started();
  connect_or_defer_until_disconnected();
}

void wifi_manager_reconnect_sta() { connect_or_defer_until_disconnected(); }

std::array<uint8_t, 6> wifi_manager_ap_mac() {
  std::array<uint8_t, 6> mac{};
  const esp_err_t err = esp_wifi_get_mac(WIFI_IF_AP, mac.data());
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "esp_wifi_get_mac failed: %s", esp_err_to_name(err));
  }
  return mac;
}

}  // namespace wifi_provision
