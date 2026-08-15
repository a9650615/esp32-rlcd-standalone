#pragma once

#include <cstdint>

namespace ota {

// What the rollback guard should do about the image it is currently running.
enum class RollbackDecision : uint8_t {
  // Leave the slot alone. Either this is not a freshly written image, or its
  // state could not be read - both cases must be inert, because marking an
  // image valid is irreversible for that boot and rolling one back costs a
  // reboot.
  None,
  // The image proved itself; cancel the pending rollback so the next reset
  // stays on this slot.
  MarkValid,
  // The image did not prove itself. Roll back and reboot deliberately rather
  // than waiting for a reset that may never come.
  Rollback,
};

// This board cannot lean on the watchdog to convert a hang into a rollback:
// CONFIG_ESP_TASK_WDT_PANIC is off, so a stalled LVGL task logs every five
// seconds forever and never resets. A bad image would therefore sit in
// PENDING_VERIFY indefinitely - never valid, never rolled back. The guard has
// to reach its own verdict from positive evidence and act on it.
//
// `state_readable` false covers a partition whose state could not be queried;
// `pending_verify` is true only for ESP_OTA_IMG_PENDING_VERIFY. `alive` is the
// evidence: the LVGL render loop advanced during the observation window.
constexpr RollbackDecision rollback_decision(bool state_readable,
                                             bool pending_verify, bool alive) {
  if (!state_readable || !pending_verify) return RollbackDecision::None;
  return alive ? RollbackDecision::MarkValid : RollbackDecision::Rollback;
}

}  // namespace ota
