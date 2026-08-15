#pragma once

#include <cstdint>
#include <string>

#include <esp_err.h>

#include "ota_decision.hpp"

namespace ota {

// True when the running image has been written but not yet confirmed, i.e.
// ESP_OTA_IMG_PENDING_VERIFY. False for the factory image and for any slot
// already marked valid, which is the normal steady state and must stay inert.
//
// `readable` is set false when the partition state could not be queried at
// all; callers must treat that as "do nothing" rather than as "not pending",
// which is why it is reported separately instead of folded into the result.
bool pending_verify(bool& readable);

// Cancels the pending rollback for the running image. Only meaningful while
// pending_verify() is true; harmless otherwise.
esp_err_t mark_valid();

// Marks the running image bad and reboots into the previous one. Does not
// return on success. Used instead of waiting for a reset because this board
// runs its task watchdog without panic, so a hang never resets by itself.
void rollback_and_reboot();

// True when some app slot is recorded as INVALID or ABORTED, which is what the
// bootloader leaves behind when it rejects an image.
//
// This is evidence that an update was rejected at some point, not proof that
// it happened on the immediately preceding boot: the marking persists in
// otadata until that slot is written again. It is reported anyway because the
// alternative is that a rejected update leaves no trace a user can ever see.
bool update_was_rejected();

// Human-readable running-slot name ("factory", "ota_0", ...) for logs and for
// the on-screen detail line. Empty when it cannot be determined.
std::string running_slot_name();

}  // namespace ota
