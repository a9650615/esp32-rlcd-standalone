#include "ota.hpp"

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

namespace ota {
namespace {

constexpr char kTag[] = "ota";

}  // namespace

bool pending_verify(bool& readable) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) {
    readable = false;
    return false;
  }
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    // The factory partition has no otadata entry of its own, so this is the
    // normal answer on a factory boot rather than a fault. Either way the
    // guard must stay inert, which is what readable=false asks for.
    readable = false;
    return false;
  }
  readable = true;
  return state == ESP_OTA_IMG_PENDING_VERIFY;
}

esp_err_t mark_valid() {
  const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
  if (result == ESP_OK) {
    ESP_LOGI(kTag, "image marked valid; rollback cancelled");
  } else {
    ESP_LOGE(kTag, "marking image valid failed: %s", esp_err_to_name(result));
  }
  return result;
}

void rollback_and_reboot() {
  ESP_LOGE(kTag, "image failed its verification window; rolling back");
  const esp_err_t result = esp_ota_mark_app_invalid_rollback_and_reboot();
  // Only reached when there is nothing to roll back to. Rebooting anyway
  // would just re-run the same unverified image, so stay put and leave the
  // board diagnosable over serial.
  ESP_LOGE(kTag, "rollback unavailable: %s", esp_err_to_name(result));
}

bool update_was_rejected() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_partition_iterator_t it = esp_partition_find(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  bool rejected = false;
  while (it != nullptr) {
    const esp_partition_t* part = esp_partition_get(it);
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    // Skip the slot we booted from: it is either valid or still pending, and
    // the rollback guard owns that verdict.
    if (part != running &&
        esp_ota_get_state_partition(part, &state) == ESP_OK &&
        (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED)) {
      ESP_LOGW(kTag, "slot %s is recorded as a rejected image", part->label);
      rejected = true;
    }
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  return rejected;
}

std::string running_slot_name() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running != nullptr ? std::string(running->label) : std::string();
}

}  // namespace ota
