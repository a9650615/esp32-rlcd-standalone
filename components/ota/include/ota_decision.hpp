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
// `pending_verify` is true only for ESP_OTA_IMG_PENDING_VERIFY.
//
// Two pieces of evidence, both required:
//
// `renders` - the LVGL render loop advanced during the observation window.
//
// `reachable` - the station holds an IP. Rendering alone was the whole test
// once, and it misses the failure that matters most to a board developed
// without a cable: an image that draws perfectly and has broken its own
// networking marks itself valid and can never be reached again. Recovering
// from that needs USB, which is the one thing the remote workflow does not
// have.
//
// This does not say the board needs Wi-Fi in general - it runs standalone
// without it by design. It says an image that *arrived over the network* has
// to prove the network still works, and an image is only ever in
// PENDING_VERIFY because it arrived that way.
//
// The two failure directions are not symmetric, which is what settles the
// close call. Too strict costs a rollback to the previous working image and a
// re-push, when for instance the access point happened to be down during the
// window. Too lenient costs a board that answers nothing until someone brings
// a cable to it.
constexpr RollbackDecision rollback_decision(bool state_readable,
                                             bool pending_verify, bool renders,
                                             bool reachable) {
  if (!state_readable || !pending_verify) return RollbackDecision::None;
  return renders && reachable ? RollbackDecision::MarkValid
                              : RollbackDecision::Rollback;
}

}  // namespace ota
