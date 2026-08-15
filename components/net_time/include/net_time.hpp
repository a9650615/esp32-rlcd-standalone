#pragma once

#include "net_time_convert.hpp"

#include <esp_err.h>

namespace net_time {

// Starts SNTP against pool.ntp.org. Requires esp_netif_init() and
// esp_event_loop_create_default() to already have run - wifi_provision::
// start() does both - but is otherwise safe to call before the station has
// an IP: SNTP just queues requests until the network is up. Idempotent.
esp_err_t start();

// True once a sync has actually landed, not merely been requested.
bool synced();

// Fills out in Taiwan local time from the current system clock. Returns
// false, leaving out untouched, until synced() is true.
bool now(app_core::RtcDateTime& out);

}  // namespace net_time
