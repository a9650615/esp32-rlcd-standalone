#pragma once

#include "history.hpp"

#include <esp_err.h>

// Keeps the battery and sensor history across reboots, in the `storage`
// partition that has been reserved and unused since the partition table was
// written.
//
// WEAR, because this is the question that decides whether an SD card is
// needed, and the answer is no by several orders of magnitude:
//
//   One 4 KiB sector is erased and rewritten per save. Saves happen once per
//   five-minute slot, so 288/day. The partition is 1 MiB = 256 sectors, and
//   saves rotate through all of them, so each sector is erased 288/256 = 1.13
//   times per day. NOR flash of this class is rated 100 000 erase cycles per
//   sector, giving ~88 000 days - about 240 years. Even against a pessimistic
//   10 000-cycle part it is 24 years, which is longer than the cell, the
//   panel, or the interest.
//
// What would ruin that is writing per sample instead of per slot, or writing
// to a fixed sector instead of rotating. Both are easy to introduce by
// accident and neither fails visibly - the flash just wears out a year or two
// later. A/B alternation across two sectors, an obvious-looking design, lands
// at 2.8 years on a 100 000-cycle part and about three months on a 10 000-cycle
// one.
//
// An SD card would be a consumable with a socket, a filesystem, and a failure
// mode that arrives without warning, in exchange for capacity that is already
// 200x more than this needs.
namespace history_store {

// Finds the newest sector that passes its own checksum and loads it. Safe to
// call before anything else; a partition that is missing, empty, or entirely
// corrupt simply yields an empty history rather than an error the caller has
// to handle.
//
// Returns ESP_ERR_NOT_FOUND only when the partition itself is absent, which
// means the partition table on the device predates this feature.
esp_err_t init();

// The in-RAM copy. Callers read it directly; it is only mutated by record().
const app_core::HistoryBlob& current();

// Appends a slot and writes the whole ring to the next sector in rotation.
//
// Not called per reading: the callers sample far faster than the slot interval
// and are expected to aggregate. See the wear note above for why that matters.
esp_err_t record(const app_core::HistorySample& sample);

// Erases every sector. Exposed for the settings page rather than for tests -
// history that survived a calibration change is history measured against a
// different voltage scale.
esp_err_t clear();

}  // namespace history_store
