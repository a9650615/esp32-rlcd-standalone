#include "tray_registry.hpp"

#include <atomic>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

namespace app_core {
namespace {

#ifdef ESP_PLATFORM
constexpr char kTag[] = "tray_registry";
#endif

// Both booleans are atomic, not because this is a hot path, but because
// register_tray_indicator() (called once, at a module's init) and
// tray_indicator_slot() (called every ~100ms from the LVGL task) run on
// different tasks with no other synchronization between them, and
// set_tray_indicator_active() is explicitly documented as callable from
// any task. Default std::atomic operations (sequentially consistent) are
// simple to reason about and cheap enough at this frequency - no need to
// hand-roll a weaker memory order for a handful of flag flips a second.
struct Slot {
  std::atomic<bool> registered{false};
  std::atomic<bool> active{false};
  TrayIndicatorBitmap bitmap;
};

Slot g_slots[kMaxTrayIndicators];

}  // namespace

TrayIndicatorHandle register_tray_indicator(const TrayIndicatorBitmap& bitmap) {
  if (bitmap.pixels == nullptr || bitmap.width != kTrayIconSize ||
      bitmap.height != kTrayIconSize || bitmap.byte_count != kTrayIconBitmapBytes) {
    return TrayIndicatorHandle{};
  }

  for (int i = 0; i < kMaxTrayIndicators; ++i) {
    if (g_slots[i].registered.load()) continue;
    // bitmap written before registered flips true, so a reader that
    // observes registered==true is guaranteed to see the real bitmap, not
    // a half-written one.
    g_slots[i].bitmap = bitmap;
    g_slots[i].active.store(false);
    g_slots[i].registered.store(true);
    return TrayIndicatorHandle{static_cast<int8_t>(i)};
  }
  return TrayIndicatorHandle{};  // full
}

void set_tray_indicator_active(TrayIndicatorHandle handle, bool active) {
  if (!handle.valid() || handle.slot >= kMaxTrayIndicators) {
    // Loud, not a quiet return: an unwired or invalid handle reaching here
    // is a caller bug (a handle captured before registration, or one from a
    // registry that was full), and it must not look identical in the log to
    // a genuine, successful no-op. See modules/audio/README.md's "activation
    // never happens" section for the failure mode this exists to make loud.
#ifdef ESP_PLATFORM
    ESP_LOGW(kTag, "set_tray_indicator_active ignored: invalid handle (slot=%d)",
             static_cast<int>(handle.slot));
#endif
    return;
  }
  g_slots[handle.slot].active.store(active);
}

TrayIndicatorSlot tray_indicator_slot(int index) {
  if (index < 0 || index >= kMaxTrayIndicators || !g_slots[index].registered.load()) {
    return TrayIndicatorSlot{};
  }
  return TrayIndicatorSlot{true, g_slots[index].active.load(), g_slots[index].bitmap};
}

void reset_tray_registry_for_test() {
  for (int i = 0; i < kMaxTrayIndicators; ++i) {
    g_slots[i].registered.store(false);
    g_slots[i].active.store(false);
    g_slots[i].bitmap = TrayIndicatorBitmap{};
  }
}

}  // namespace app_core
