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
// True once the station holds an IP. Network providers must gate their first
// fetch on this; at boot they are running well before DHCP finishes.
bool station_has_ip();

void set_battery(const app_core::BatteryData& battery);

// Same pattern as set_battery: callable from any task, merges the one field
// into the shared snapshot and republishes. Keeps this component the single
// AppSnapshot owner/publisher rather than letting each provider task manage
// its own copy.
void set_indoor(const app_core::IndoorData& indoor);
void set_weather(const app_core::WeatherData& weather);
void set_taiwan_market(const app_core::MarketData& market);
void set_us_market(const app_core::MarketData& market);
void set_clock(const app_core::ClockData& clock);
void set_ota(const app_core::OtaData& ota);

// Registers what GET /shot returns: the panel's current framebuffer, 1 bit per
// pixel, 400x300. board::framebuffer_snapshot has exactly this shape, so main
// registers it directly rather than wrapping it.
//
// An indirection rather than a call into board_rlcd, for the same reason
// set_ota is one in the other direction: this component owns the network, not
// the display, and a networking component that reaches into the panel driver
// inverts the layering the rest of the file is careful about.
//
// Debug builds only - the route is not registered at all in a release build,
// so a device in the field does not serve pictures of its screen to the LAN.
void set_screenshot_provider(bool (*provider)(uint8_t* out, size_t length));

}  // namespace wifi_provision
