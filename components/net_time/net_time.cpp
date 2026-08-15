#include "net_time.hpp"

#include <atomic>
#include <cstdlib>
#include <ctime>

#include <esp_log.h>
#include <esp_netif_sntp.h>

namespace net_time {
namespace {

constexpr char kTag[] = "net_time";
constexpr char kNtpServer[] = "pool.ntp.org";

std::atomic<bool> g_synced{false};
bool g_started = false;

// esp_netif fires this on every resync, not just the first; only the
// transition into synced is worth a log line.
void on_sync(struct timeval* tv) {
  if (g_synced.exchange(true)) return;
  app_core::RtcDateTime decoded{};
  epoch_to_local(tv->tv_sec, decoded);
  ESP_LOGI(kTag, "NTP sync landed: %04u-%02u-%02u %02u:%02u:%02u local",
           decoded.year, decoded.month, decoded.day, decoded.hour,
           decoded.minute, decoded.second);
}

}  // namespace

esp_err_t start() {
  if (g_started) return ESP_OK;
  g_started = true;

  setenv("TZ", kTimeZone, 1);
  tzset();

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(kNtpServer);
  config.sync_cb = &on_sync;
  const esp_err_t result = esp_netif_sntp_init(&config);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "esp_netif_sntp_init failed: %s", esp_err_to_name(result));
  }
  return result;
}

bool synced() { return g_synced.load(); }

bool now(app_core::RtcDateTime& out) {
  if (!g_synced.load()) return false;
  epoch_to_local(std::time(nullptr), out);
  return true;
}

}  // namespace net_time
