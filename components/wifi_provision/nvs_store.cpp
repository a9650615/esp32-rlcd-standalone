#include "internal.hpp"

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace wifi_provision {
namespace {

constexpr char kTag[] = "nvs_store";
constexpr char kNamespace[] = "wifi_cfg";
constexpr char kSsidKey[] = "ssid";
constexpr char kPassKey[] = "pass";

}  // namespace

esp_err_t nvs_store_init() {
  esp_err_t result = nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
      result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // Only the NVS partition is erased, never the whole flash.
    esp_err_t erase_result = nvs_flash_erase();
    if (erase_result != ESP_OK) return erase_result;
    result = nvs_flash_init();
  }
  return result;
}

bool nvs_load(wifi_config::Credentials& out) {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;

  char ssid[33] = {};
  size_t ssid_len = sizeof(ssid);
  const bool has_ssid =
      nvs_get_str(handle, kSsidKey, ssid, &ssid_len) == ESP_OK;

  char pass[64] = {};
  size_t pass_len = sizeof(pass);
  if (nvs_get_str(handle, kPassKey, pass, &pass_len) != ESP_OK) pass[0] = '\0';

  nvs_close(handle);
  if (!has_ssid) {
    ESP_LOGI(kTag, "boot: no saved credentials");
    return false;
  }
  // ssid is public (broadcast in the clear); password is never logged.
  ESP_LOGI(kTag, "boot: loaded credentials, ssid=%s", ssid);
  out.ssid = ssid;
  out.password = pass;
  return true;
}

esp_err_t nvs_save(const wifi_config::Credentials& creds) {
  nvs_handle_t handle;
  esp_err_t result = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (result != ESP_OK) return result;

  // Explicit clear path: erase both keys first so a shorter or absent new
  // password never leaves a stale value behind.
  ESP_LOGI(kTag, "clearing previous credentials before save");
  for (const char* key : {kSsidKey, kPassKey}) {
    esp_err_t erase_result = nvs_erase_key(handle, key);
    if (erase_result != ESP_OK && erase_result != ESP_ERR_NVS_NOT_FOUND) {
      nvs_close(handle);
      ESP_LOGW(kTag, "clear failed: %s", esp_err_to_name(erase_result));
      return erase_result;
    }
  }

  result = nvs_set_str(handle, kSsidKey, creds.ssid.c_str());
  if (result == ESP_OK && !creds.password.empty()) {
    result = nvs_set_str(handle, kPassKey, creds.password.c_str());
  }
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  if (result == ESP_OK) {
    ESP_LOGI(kTag, "saved credentials, ssid=%s", creds.ssid.c_str());
  } else {
    ESP_LOGW(kTag, "save failed: %s", esp_err_to_name(result));
  }
  return result;
}

}  // namespace wifi_provision
