#pragma once

#include "app_snapshot.hpp"

#ifdef ESP_PLATFORM

namespace board {

// Samples the GPIO4 / ADC1 channel 3 battery divider (averaging a few reads
// to damp ADC noise), converts through app_core::battery_millivolts() and
// battery_percent(), and fills out. Returns false only on a hard ADC
// failure; an implausible reading still returns true with out.valid ==
// false (see app_core::kBatteryValidThresholdMillivolts).
bool battery_read(app_core::BatteryData& out);

}  // namespace board

#endif  // ESP_PLATFORM
