#pragma once

// Private wiring between this component's translation units. Not part of
// the public API in include/.

#include "wifi_config.hpp"

#include <esp_err.h>

#include <array>
#include <cstdint>
#include <string>

namespace wifi_provision {

// Why the STA link dropped, so the status text can be specific.
enum class DisconnectReason { None, AuthFailure, NotFound, Other };

// Log-friendly name; shared by wifi_manager.cpp (raw event) and
// wifi_provision.cpp (state-machine trail) so both sides agree on wording.
inline const char* to_string(DisconnectReason reason) {
  switch (reason) {
    case DisconnectReason::None: return "None";
    case DisconnectReason::AuthFailure: return "AuthFailure";
    case DisconnectReason::NotFound: return "NotFound";
    case DisconnectReason::Other: return "Other";
  }
  return "Unknown";
}

// --- nvs_store.cpp ---
// Handles nvs_flash_init() once, tolerating a full/stale partition by
// erasing and re-initializing only the NVS partition. Call before load/save.
esp_err_t nvs_store_init();
// false means no saved ssid (namespace empty or absent).
bool nvs_load(wifi_config::Credentials& out);
// Erases both keys then writes the new ones and commits.
esp_err_t nvs_save(const wifi_config::Credentials& creds);

// --- wifi_manager.cpp ---
// esp_netif/esp_event/esp_wifi init plus the Wi-Fi/IP event handlers, which
// call handle_wifi_connected()/handle_wifi_disconnected() below.
esp_err_t wifi_manager_init();
// Open (unauthenticated) AP named ssid; leaves STA alone if it is already
// connecting. Joining is a single tap - the setup portal itself is what's
// password-gated, see current_portal_password() below.
void wifi_manager_start_ap(const std::string& ssid);
// Drops the AP interface; STA (already connected) is unaffected.
void wifi_manager_stop_ap();
// Configures and (re)starts the STA connection attempt.
void wifi_manager_connect_sta(const wifi_config::Credentials& creds);
// Retries the STA connection with the already-configured credentials.
void wifi_manager_reconnect_sta();
std::array<uint8_t, 6> wifi_manager_ap_mac();

// --- wifi_provision.cpp, called from wifi_manager.cpp's event handlers ---
void handle_wifi_connected();
void handle_wifi_disconnected(DisconnectReason reason);

// --- wifi_provision.cpp, read/called by portal.cpp ---
std::string current_ap_ssid();
std::string current_status_text();
// Per-session page password gating the setup portal; empty only when setup
// mode isn't active. Never logged.
std::string current_portal_password();
// Called from the POST handler on a valid submission.
void handle_credentials_saved(const wifi_config::Credentials& creds);

// --- portal.cpp ---
// HTTP server + wildcard DNS responder; only while SetupAp is active.
void portal_start();
void portal_stop();

}  // namespace wifi_provision
