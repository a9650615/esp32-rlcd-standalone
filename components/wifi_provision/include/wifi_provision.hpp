#pragma once

#include "app_snapshot.hpp"

#include <esp_err.h>

namespace wifi_provision {

// Owns NVS credentials, esp_wifi, the setup AP, the DNS responder and the
// HTTP portal. Takes a copy of the current snapshot and republishes it
// through ui::publish_snapshot() whenever provisioning state changes. Call
// after LVGL and UI startup have completed.
esp_err_t start(const app_core::AppSnapshot& snapshot);

// Registered as ui::set_setup_gesture_handler; safe to call from the LVGL
// thread.
void toggle_setup();

// Callable from any task (e.g. the periodic battery-sampling loop in
// app_main). Merges battery into the shared snapshot this component already
// owns and republishes via ui::publish_snapshot() without touching
// provisioning state, keeping a single snapshot publisher.
void set_battery(const app_core::BatteryData& battery);

}  // namespace wifi_provision
