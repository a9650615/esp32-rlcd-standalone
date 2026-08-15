#include "net_log.hpp"
#include "net_log_ring.hpp"

#include "sdkconfig.h"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#ifdef CONFIG_NET_LOG_ENABLE
#include <lwip/sockets.h>
#endif

#ifndef CONFIG_NET_LOG_PORT
#define CONFIG_NET_LOG_PORT 3334
#endif
#ifndef CONFIG_NET_LOG_RING_BYTES
#define CONFIG_NET_LOG_RING_BYTES 32768
#endif

namespace net_log {

std::uint16_t port() { return CONFIG_NET_LOG_PORT; }

#ifdef CONFIG_NET_LOG_ENABLE

namespace {

constexpr char kTag[] = "net_log";

LineRing* g_ring = nullptr;
SemaphoreHandle_t g_ring_mutex = nullptr;
vprintf_like_t g_original_vprintf = nullptr;

// Installed via esp_log_set_vprintf(). Called from whatever task happened
// to log, possibly concurrently from several - it must never block, never
// allocate unboundedly, and never itself call ESP_LOG* (that would recurse
// straight back in here).
int net_vprintf(const char* format, va_list args) {
  int written = 0;
  if (g_original_vprintf) {
    // Serial is augmented, never replaced: run the previous sink (the
    // default UART0 one) first, on its own va_list copy, unconditionally.
    va_list original_args;
    va_copy(original_args, args);
    written = g_original_vprintf(format, original_args);
    va_end(original_args);
  }

  // Fixed stack buffer, no heap: this is the same fully-formatted line
  // (level/timestamp/tag/message, optionally colored) UART already
  // receives. Longer than this is truncated here, not dropped outright.
  char line[256];
  const int formatted = std::vsnprintf(line, sizeof(line), format, args);
  if (formatted > 0) {
    const std::size_t len =
        std::min(static_cast<std::size_t>(formatted), sizeof(line) - 1);
    // Try-lock only (0 ticks): a contended mutex drops this one line
    // rather than blocking the caller, which could be any task, including
    // one with hard timing to keep.
    if (g_ring_mutex != nullptr && xSemaphoreTake(g_ring_mutex, 0) == pdTRUE) {
      g_ring->push(line, len);
      xSemaphoreGive(g_ring_mutex);
    }
  }
  return written;
}

// Single small task: owns the listening socket, accepts one operator at a
// time (a second connection replaces the first - this is a diagnostic
// aid, not a multi-viewer service), and drains newly retained lines to
// whichever client is current. A fresh connection resets its send cursor
// to 0, which LineRing::read_line() clamps forward to the oldest still-
// retained line, so a viewer that connects after a failure replays the
// backlog leading up to it before it catches up to the live tail.
[[noreturn]] void sender_task(void*) {
  const int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (listen_fd < 0) {
    ESP_LOGE(kTag, "socket() failed: errno %d", errno);
    vTaskDelete(nullptr);
  }
  int reuse = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(CONFIG_NET_LOG_PORT);
  if (bind(listen_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
      0) {
    ESP_LOGE(kTag, "bind() on port %d failed: errno %d", CONFIG_NET_LOG_PORT,
             errno);
    close(listen_fd);
    vTaskDelete(nullptr);
  }
  listen(listen_fd, 1);

  int client_fd = -1;
  std::uint64_t send_cursor = 0;

  for (;;) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(listen_fd, &read_fds);
    int max_fd = listen_fd;
    if (client_fd >= 0) {
      FD_SET(client_fd, &read_fds);
      if (client_fd > max_fd) max_fd = client_fd;
    }
    // Bounded wait so the loop still wakes on its own to drain freshly
    // pushed lines even with no socket activity at all.
    timeval poll_timeout{0, 200 * 1000};
    const int ready =
        select(max_fd + 1, &read_fds, nullptr, nullptr, &poll_timeout);

    if (ready > 0 && FD_ISSET(listen_fd, &read_fds)) {
      const int accepted = accept(listen_fd, nullptr, nullptr);
      if (accepted >= 0) {
        if (client_fd >= 0) close(client_fd);
        // Bounded send timeout: a stalled/vanished client must never hang
        // this task indefinitely.
        timeval send_timeout{0, 200 * 1000};
        setsockopt(accepted, SOL_SOCKET, SO_SNDTIMEO, &send_timeout,
                   sizeof(send_timeout));
        client_fd = accepted;
        send_cursor = 0;
        char banner[96];
        const int banner_len = std::snprintf(
            banner, sizeof(banner),
            "-- net_log connected; %lu line(s) dropped since boot --\n",
            static_cast<unsigned long>(g_ring->dropped_lines()));
        if (banner_len > 0) send(client_fd, banner, banner_len, 0);
      }
    }
    if (ready > 0 && client_fd >= 0 && FD_ISSET(client_fd, &read_fds)) {
      char scratch[16];
      if (recv(client_fd, scratch, sizeof(scratch), 0) <= 0) {
        close(client_fd);
        client_fd = -1;
      }
    }

    if (client_fd < 0) continue;
    for (;;) {
      char line[256];
      std::size_t line_len = 0;
      xSemaphoreTake(g_ring_mutex, portMAX_DELAY);
      const bool has_line =
          g_ring->read_line(send_cursor, line, sizeof(line), line_len);
      xSemaphoreGive(g_ring_mutex);
      if (!has_line) break;
      // ponytail: a failed/partial send here is treated as delivered (the
      // cursor already advanced) rather than retried - simplest recovery
      // is just letting the operator reconnect, which replays the backlog.
      if (send(client_fd, line, line_len, 0) < 0) {
        close(client_fd);
        client_fd = -1;
        break;
      }
    }
  }
}

}  // namespace

esp_err_t start() {
  static bool started = false;
  if (started) return ESP_OK;

  auto* storage = static_cast<std::uint8_t*>(
      heap_caps_malloc(CONFIG_NET_LOG_RING_BYTES, MALLOC_CAP_SPIRAM));
  if (storage == nullptr) {
    ESP_LOGE(kTag, "PSRAM allocation of %d bytes for the log ring failed",
             CONFIG_NET_LOG_RING_BYTES);
    return ESP_ERR_NO_MEM;
  }
  g_ring = new LineRing(storage, CONFIG_NET_LOG_RING_BYTES);

  g_ring_mutex = xSemaphoreCreateMutex();
  if (g_ring_mutex == nullptr) {
    ESP_LOGE(kTag, "mutex creation failed");
    return ESP_ERR_NO_MEM;
  }

  if (xTaskCreate(&sender_task, "net_log_sender", 4096, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag, "sender task creation failed");
    return ESP_ERR_NO_MEM;
  }

  // Installed last, once the ring/mutex/sender it depends on all exist.
  g_original_vprintf = esp_log_set_vprintf(&net_vprintf);
  started = true;

  esp_netif_ip_info_t ip_info{};
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  const esp_err_t ip_result =
      netif != nullptr ? esp_netif_get_ip_info(netif, &ip_info) : ESP_FAIL;
  if (ip_result == ESP_OK) {
    ESP_LOGI(kTag, "listening on " IPSTR ":%d - connect with: nc " IPSTR " %d",
             IP2STR(&ip_info.ip), CONFIG_NET_LOG_PORT, IP2STR(&ip_info.ip),
             CONFIG_NET_LOG_PORT);
  } else {
    ESP_LOGW(kTag, "listening on port %d but station IP lookup failed: %s",
             CONFIG_NET_LOG_PORT, esp_err_to_name(ip_result));
  }
  return ESP_OK;
}

std::uint32_t dropped_lines() {
  return g_ring != nullptr ? g_ring->dropped_lines() : 0;
}

#else  // !CONFIG_NET_LOG_ENABLE

esp_err_t start() { return ESP_ERR_NOT_SUPPORTED; }
std::uint32_t dropped_lines() { return 0; }

#endif  // CONFIG_NET_LOG_ENABLE

}  // namespace net_log
