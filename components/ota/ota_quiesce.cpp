#include "ota_quiesce.hpp"

#include <cstdint>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

namespace ota {
namespace {

#ifdef ESP_PLATFORM
constexpr char kTag[] = "ota";
#endif

QuiesceHook g_hooks[kMaxQuiesceHooks] = {};
int g_hook_count = 0;

// esp_log_timestamp() rather than esp_timer_get_time(): it comes with
// <esp_log.h>, which this file needs anyway, so the budget costs the ota
// component no new REQUIRES entry. Milliseconds since boot is exactly the
// resolution a 500 ms budget wants.
//
// Host builds have no clock here and return 0, which makes the budget
// unreachable - correct for a test, which asserts that every hook ran rather
// than that the clock works.
uint32_t now_ms() {
#ifdef ESP_PLATFORM
  return esp_log_timestamp();
#else
  return 0;
#endif
}

}  // namespace

bool register_quiesce_hook(QuiesceHook hook) {
  if (hook == nullptr) return false;
  if (g_hook_count >= kMaxQuiesceHooks) return false;
  g_hooks[g_hook_count++] = hook;
  return true;
}

int quiesce_hook_count() { return g_hook_count; }

int run_quiesce_hooks() {
  const uint32_t started_ms = now_ms();
  int called = 0;
  for (int index = 0; index < g_hook_count; ++index) {
    const uint32_t elapsed_ms = now_ms() - started_ms;
    if (elapsed_ms > static_cast<uint32_t>(kQuiesceBudgetMs)) {
      // Named, not merely counted: the point of the log is that the next
      // person knows which module to look at, and how many never ran.
#ifdef ESP_PLATFORM
      ESP_LOGW(kTag,
               "quiesce budget spent after %d of %d hooks (%u ms of %d); "
               "starting the write with the rest unrun",
               called, g_hook_count, static_cast<unsigned>(elapsed_ms),
               kQuiesceBudgetMs);
#endif
      break;
    }
    g_hooks[index]();
    ++called;
  }
#ifdef ESP_PLATFORM
  if (called > 0) {
    ESP_LOGI(kTag, "quiesced %d module(s) in %u ms before the write", called,
             static_cast<unsigned>(now_ms() - started_ms));
  }
#endif
  return called;
}

void reset_quiesce_hooks_for_test() {
  for (int index = 0; index < kMaxQuiesceHooks; ++index) {
    g_hooks[index] = nullptr;
  }
  g_hook_count = 0;
}

}  // namespace ota
