#include "net_log.hpp"
#include "net_log_ring.hpp"

#include "sdkconfig.h"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_pm.h>
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

  // Observing this board must not be what makes it unobservable.
  //
  // With automatic light sleep the core sleeps between beacons, and this
  // task's outbound sends wait for a wake window each time. Measured: a
  // 400-second capture delivered 103 lines and reached the 11th second of the
  // log - about a quarter of a line per second. Shrinking the replay ring
  // first was the wrong guess; the live stream is what crawls. On a board with
  // no cable that is not a slow log, it is no log.
  //
  // So the lock is held exactly while somebody is connected, which is the
  // whole cost model: watching costs power, and nobody watches an idle clock
  // for days. Idle standby - the number all of this power work exists for - is
  // untouched, because with no client the lock is not held.
  //
  // Not a blanket "no light sleep when net_log is compiled in": that would
  // spend the saving permanently to make a facility that is used minutes a day
  // fast.
#if CONFIG_PM_ENABLE
  esp_pm_lock_handle_t no_light_sleep = nullptr;
  bool holding_lock = false;
  if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "net_log",
                         &no_light_sleep) != ESP_OK) {
    // Logged, not fatal, and deliberately not retried: the streaming path
    // still works without it, just slowly. A board that refuses to serve logs
    // because it could not create a power lock is strictly worse.
    ESP_LOGW(kTag, "no-light-sleep lock unavailable; streaming will be slow");
    no_light_sleep = nullptr;
  }
  const auto hold_for_client = [&](bool wanted) {
    if (no_light_sleep == nullptr || wanted == holding_lock) return;
    const esp_err_t err = wanted ? esp_pm_lock_acquire(no_light_sleep)
                                 : esp_pm_lock_release(no_light_sleep);
    if (err != ESP_OK) {
      ESP_LOGW(kTag, "no-light-sleep lock %s failed: %s",
               wanted ? "acquire" : "release", esp_err_to_name(err));
      return;
    }
    holding_lock = wanted;
    ESP_LOGI(kTag, "light sleep %s while an operator is %s",
             wanted ? "held off" : "allowed again",
             wanted ? "connected" : "gone");
  };
#else
  const auto hold_for_client = [](bool) {};
#endif
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
        hold_for_client(true);
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
        hold_for_client(false);
      }
    }

    if (client_fd < 0) continue;
    for (;;) {
      char line[256];
      std::size_t line_len = 0;
      // Kept so a send that could not go out can be retried from the same
      // place next time round rather than losing the line.
      const std::uint64_t cursor_before = send_cursor;
      xSemaphoreTake(g_ring_mutex, portMAX_DELAY);
      const bool has_line =
          g_ring->read_line(send_cursor, line, sizeof(line), line_len);
      xSemaphoreGive(g_ring_mutex);
      if (!has_line) break;
      if (send(client_fd, line, line_len, 0) >= 0) continue;

      // EAGAIN here is the 200 ms SO_SNDTIMEO expiring against a full TCP
      // window - backpressure, not a dead client. It happens whenever the
      // firmware logs faster than the link drains, which a screenshot dump
      // does by design: 157 lines with nothing between them.
      //
      // Closing on it was dropping an operator's capture mid-stream after
      // about forty seconds, which reads as an unreliable board rather than
      // as flow control working. Rewind and let the next pass retry; the
      // 200 ms select timeout above is the pause that lets the window open.
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        send_cursor = cursor_before;
        break;
      }
      // Anything else is the client really having gone.
      close(client_fd);
      client_fd = -1;
      break;
    }
  }
}

}  // namespace

esp_err_t begin() {
  static bool begun = false;
  if (begun) return ESP_OK;

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

  // From here every log line is retained. Nothing sends yet; the sender task
  // does not exist until start(), and until then this is purely a buffer that
  // an operator will be handed in full when they eventually connect.
  g_original_vprintf = esp_log_set_vprintf(&net_vprintf);
  begun = true;
  return ESP_OK;
}

esp_err_t start() {
  static bool started = false;
  if (started) return ESP_OK;

  // Normally already done from app_main; called here too so start() alone
  // still works and the ordering is not a trap for a future caller.
  const esp_err_t ring_ready = begin();
  if (ring_ready != ESP_OK) return ring_ready;

  if (xTaskCreate(&sender_task, "net_log_sender", 4096, nullptr,
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag, "sender task creation failed");
    return ESP_ERR_NO_MEM;
  }

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
esp_err_t begin() { return ESP_ERR_NOT_SUPPORTED; }
std::uint32_t dropped_lines() { return 0; }

#endif  // CONFIG_NET_LOG_ENABLE

}  // namespace net_log
