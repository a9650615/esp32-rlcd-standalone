#pragma once

#include <cstdint>

#include <esp_err.h>

// Mirrors ESP-IDF log output (ESP_LOGE/W/I/D/V - everything that already
// goes to serial) to a plaintext TCP port on the LAN, in addition to
// serial. Every defect found on this board so far was diagnosed from a
// serial capture, several invisible until logging was added, and serial
// requires the board tethered to a desk. This makes that window reachable
// over Wi-Fi instead.
//
// SECURITY: this is diagnostic-only, unauthenticated, unencrypted, and
// mirrors *everything* the firmware ever logs to *anyone* on the LAN who
// connects. The Wi-Fi password and the per-session setup-page password
// must never be logged anywhere in this firmware - that rule predates this
// component - but this component is exactly why a future slip would stop
// being a "someone would need a USB cable" mistake and become a "anyone on
// the LAN at the time" one. Never add anything here that dumps arbitrary
// memory, NVS, or configuration; this only ever mirrors the same lines
// serial already prints. Disabled by default (CONFIG_NET_LOG_ENABLE=n);
// an operator who wants it must opt in via `idf.py menuconfig` ->
// CONFIG_NET_LOG_ENABLE=y and rebuild.
namespace net_log {

// Installs the network log sink and starts its sender task. Serial keeps
// working unchanged - the previous esp_log_set_vprintf sink (UART0 by
// default) is always called first, on its own va_list copy, before this
// component does anything with the line. Call once the station holds an
// IP (net_log needs it to bind/log its listen address); safe to call
// again, a no-op after the first successful call.
//
// Returns ESP_ERR_NOT_SUPPORTED if CONFIG_NET_LOG_ENABLE is not set - the
// safe default - without touching the log sink or opening any socket.
esp_err_t start();

// TCP port operators connect to (CONFIG_NET_LOG_PORT), reported
// regardless of whether net_log is actually enabled/running.
std::uint16_t port();

// Lines lost since start(): ring-buffer evictions (consumer too slow or no
// one connected) plus lines too long to ever fit. 0 if never started.
std::uint32_t dropped_lines();

}  // namespace net_log
