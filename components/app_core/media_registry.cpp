#include "media_registry.hpp"

#include <mutex>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

namespace app_core {
namespace {

#ifdef ESP_PLATFORM
constexpr char kTag[] = "media_registry";
#endif

// A mutex rather than tray_registry.cpp's atomics: NowPlaying carries
// std::strings, and a torn read of one is a crash, not a stale value. The
// same reasoning ui_app.cpp's own publish mutex already documents. std::mutex
// and not a FreeRTOS semaphore so this file builds for host tests unchanged.
std::mutex g_mutex;
bool g_registered = false;
NowPlaying g_state;

}  // namespace

MediaSourceHandle register_media_source() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_registered) return MediaSourceHandle{};
  g_registered = true;
  return MediaSourceHandle{0};
}

void publish_now_playing(MediaSourceHandle handle, const NowPlaying& state) {
  if (!handle.valid()) {
#ifdef ESP_PLATFORM
    ESP_LOGW(kTag, "publish_now_playing ignored: invalid handle (slot=%d)",
             static_cast<int>(handle.slot));
#endif
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_state = state;
}

void clear_media_session(MediaSourceHandle handle) {
  if (!handle.valid()) {
#ifdef ESP_PLATFORM
    ESP_LOGW(kTag, "clear_media_session ignored: invalid handle (slot=%d)",
             static_cast<int>(handle.slot));
#endif
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_state = NowPlaying{};
}

NowPlaying now_playing() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_state;
}

void reset_media_registry_for_test() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_registered = false;
  g_state = NowPlaying{};
}

}  // namespace app_core
