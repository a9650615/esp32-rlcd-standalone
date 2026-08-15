#include "ui_app.hpp"

#include "carousel_controller.hpp"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <limits>
#include <vector>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <lvgl.h>

#include "board_buttons.hpp"
#include "lvgl_port.hpp"

namespace ui {
namespace {

constexpr char kTag[] = "ui_app";
constexpr uint32_t kTimerPeriodMs = 100;
constexpr uint64_t kMinuteMs = 60'000;

struct Runtime {
  UiContext context;
  app_core::AppSnapshot snapshot;
  app_core::RtcDateTime initial_clock;
  bool rtc_fallback = true;
  uint64_t started_ms = 0;
  uint64_t last_clock_minute = std::numeric_limits<uint64_t>::max();
  app_core::PageRegistry registry;
  std::vector<app_core::PageId> active_pages;
  app_core::CarouselState carousel;
  lv_timer_t* timer = nullptr;
  uint32_t cycle = 0;
  bool initialized = false;
};

Runtime g_runtime;

const char* page_name(app_core::PageId page) {
  switch (page) {
    case app_core::PageId::Home:
      return "Home";
    case app_core::PageId::TaiwanMarket:
      return "TaiwanMarket";
    case app_core::PageId::UsMarket:
      return "UsMarket";
    case app_core::PageId::Weather:
      return "Weather";
    case app_core::PageId::Indoor:
      return "Indoor";
  }
  return "Unknown";
}

uint8_t dwell_for(const Runtime& runtime, app_core::PageId page) {
  for (const auto& descriptor : runtime.registry.descriptors()) {
    if (descriptor.id == page) return descriptor.dwell_seconds;
  }
  return page == app_core::PageId::Home ? 30 : 12;
}

void rebuild_active_pages(Runtime& runtime) {
  runtime.active_pages = runtime.registry.page_ids();
  if (runtime.active_pages.empty()) {
    runtime.active_pages.push_back(app_core::PageId::Home);
  }
}

int days_in_month(uint16_t year, uint8_t month) {
  static constexpr std::array<uint8_t, 12> days = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 &&
      (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
    return 29;
  }
  return days[std::min<std::size_t>(month - 1, days.size() - 1)];
}

app_core::RtcDateTime clock_at(const Runtime& runtime, uint64_t now_ms) {
  app_core::RtcDateTime clock = runtime.initial_clock;
  const uint64_t elapsed_seconds =
      now_ms >= runtime.started_ms ? (now_ms - runtime.started_ms) / 1000 : 0;
  const uint64_t total_seconds = runtime.initial_clock.second + elapsed_seconds;
  const uint64_t total_minutes =
      static_cast<uint64_t>(clock.hour) * 60 + clock.minute + total_seconds / 60;
  clock.hour = static_cast<uint8_t>((total_minutes / 60) % 24);
  clock.minute = static_cast<uint8_t>(total_minutes % 60);
  clock.second = static_cast<uint8_t>(total_seconds % 60);
  uint64_t days = total_minutes / (24 * 60);
  while (days-- > 0) {
    if (++clock.day > days_in_month(clock.year, clock.month)) {
      clock.day = 1;
      if (++clock.month > 12) {
        clock.month = 1;
        ++clock.year;
      }
    }
  }
  return clock;
}

const char* weekday_name(const app_core::RtcDateTime& date) {
  // 1 Jan 2000 was a Saturday. The short loop is only used once per minute.
  uint64_t days = 0;
  for (uint16_t year = 2000; year < date.year; ++year) {
    days += (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
  }
  for (uint8_t month = 1; month < date.month; ++month) {
    days += days_in_month(date.year, month);
  }
  days += date.day - 1;
  static constexpr std::array<const char*, 7> names = {
      "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return names[(6 + days) % names.size()];
}

void update_clock(Runtime& runtime, uint64_t now_ms) {
  const uint64_t minute =
      now_ms >= runtime.started_ms ? (now_ms - runtime.started_ms) / kMinuteMs : 0;
  if (minute == runtime.last_clock_minute) return;
  runtime.last_clock_minute = minute;
  const app_core::RtcDateTime clock = clock_at(runtime, now_ms);
  static constexpr std::array<const char*, 12> month_names = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char hero[8];
  char date[32];
  std::snprintf(hero, sizeof(hero), "%02u:%02u", clock.hour, clock.minute);
  std::snprintf(date, sizeof(date), "%s, %02u %s %04u", weekday_name(clock),
                clock.day, month_names[clock.month - 1], clock.year);
  runtime.snapshot.clock.hero = hero;
  runtime.snapshot.clock.date = date;
  runtime.snapshot.clock.source = runtime.rtc_fallback ? "RTC fallback" : "PCF85063";
}

bool remove_failed_page(Runtime& runtime, app_core::PageId page) {
  const auto found = std::find(runtime.active_pages.begin(),
                               runtime.active_pages.end(), page);
  if (found == runtime.active_pages.end()) return false;
  const std::size_t index =
      static_cast<std::size_t>(found - runtime.active_pages.begin());
  runtime.active_pages.erase(found);
  if (runtime.active_pages.empty()) return false;
  if (runtime.carousel.index > index && runtime.carousel.index > 0) {
    --runtime.carousel.index;
  }
  if (runtime.carousel.index >= runtime.active_pages.size()) {
    runtime.carousel.index = 0;
  }
  return true;
}

void log_transition(const Runtime& runtime, const char* reason) {
  const app_core::PageId page =
      runtime.active_pages.empty()
          ? app_core::PageId::Home
          : runtime.active_pages[std::min(runtime.carousel.index,
                                          runtime.active_pages.size() - 1)];
  ESP_LOGI(kTag,
           "transition cycle=%" PRIu32 " page=%s reason=%s dwell_s=%u "
           "manual_until_ms=%" PRIu64,
           runtime.cycle, page_name(page), reason,
           dwell_for(runtime, page), runtime.carousel.manual_until_ms);
}

bool render_current(Runtime& runtime, uint64_t now_ms, const char* reason,
                    bool show_overlay) {
  for (std::size_t attempt = 0; attempt < runtime.active_pages.size() + 1;
       ++attempt) {
    if (runtime.active_pages.empty()) return false;
    runtime.carousel.index =
        std::min(runtime.carousel.index, runtime.active_pages.size() - 1);
    const app_core::PageId page = runtime.active_pages[runtime.carousel.index];
    const lv_obj_t* rendered = render_page(
        runtime.context, runtime.snapshot, page, safe_canvas(),
        runtime.carousel.index, runtime.active_pages.size());
    if (rendered != nullptr) {
      log_transition(runtime, reason);
      if (show_overlay) {
        (void)navigation_overlay(runtime.context, safe_canvas());
      }
      return true;
    }

    ESP_LOGE(kTag, "renderer failure page=%s; skipping for current cycle",
             page_name(page));
    if (!remove_failed_page(runtime, page)) {
      runtime.active_pages.clear();
      runtime.active_pages.push_back(app_core::PageId::Home);
      runtime.carousel.index = 0;
      runtime.carousel.manual_mode = false;
      runtime.carousel.manual_until_ms = 0;
      return false;
    }
  }
  return false;
}

void begin_cycle(Runtime& runtime, uint64_t now_ms) {
  runtime.registry.begin_cycle(runtime.snapshot);
  rebuild_active_pages(runtime);
  runtime.carousel.index = 0;
  runtime.carousel.page_started_ms = now_ms;
  ++runtime.cycle;
  ESP_LOGI(kTag, "registry cycle=%" PRIu32 " pages=%u", runtime.cycle,
           static_cast<unsigned>(runtime.active_pages.size()));
}

void timer_callback(lv_timer_t* timer) {
  auto* runtime = static_cast<Runtime*>(lv_timer_get_user_data(timer));
  if (runtime == nullptr) return;
  const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);

  if (!runtime->initialized) {
    runtime->started_ms = now_ms;
    runtime->last_clock_minute = std::numeric_limits<uint64_t>::max();
    update_clock(*runtime, now_ms);
    lv_obj_t* host = lv_obj_create(lv_screen_active());
    if (host == nullptr || !init_context(runtime->context, host)) {
      ESP_LOGE(kTag, "fatal: UI host/context initialization failed");
      if (host != nullptr) lv_obj_delete(host);
      return;
    }
    lv_obj_set_size(host, kCanvasWidth, kCanvasHeight);
    lv_obj_set_pos(host, 0, 0);
    apply_surface(host);
    begin_cycle(*runtime, now_ms);
    runtime->carousel.index = 0;
    runtime->initialized = true;
    if (!render_current(*runtime, now_ms, "startup", false)) {
      ESP_LOGE(kTag, "fatal: initial page render failed");
      return;
    }
  }

  QueueHandle_t queue = board::button_event_queue();
  if (queue != nullptr) {
    board::ButtonEvent event;
    while (xQueueReceive(queue, &event, 0) == pdTRUE) {
      const bool key = event == board::ButtonEvent::Next;
      const char* reason = key ? "manual-key" : "manual-boot";
      ESP_LOGI(kTag, "button event=%s", key ? "KEY" : "BOOT");
      const auto transition = key
                                  ? app_core::carousel::next(
                                        runtime->carousel, now_ms,
                                        runtime->active_pages.size())
                                  : app_core::carousel::previous(
                                        runtime->carousel, now_ms,
                                        runtime->active_pages.size());
      runtime->carousel = transition.state;
      if (transition.page_changed) {
        (void)render_current(*runtime, now_ms, reason, true);
      }
    }
  }

  const app_core::PageId current_page =
      runtime->active_pages[runtime->carousel.index];
  const auto transition = app_core::carousel::tick(
      runtime->carousel, now_ms, dwell_for(*runtime, current_page),
      runtime->active_pages.size());
  const bool manual_timeout = runtime->carousel.manual_mode &&
                              transition.page_changed &&
                              !transition.state.manual_mode;
  const bool wrapped =
      transition.page_changed && !runtime->carousel.manual_mode &&
      runtime->carousel.index + 1 >= runtime->active_pages.size() &&
      transition.state.index == 0;
  runtime->carousel = transition.state;
  if (transition.page_changed) {
    if (wrapped) begin_cycle(*runtime, now_ms);
    (void)render_current(*runtime, now_ms,
                         wrapped ? "cycle" :
                                    (manual_timeout ? "manual-timeout" : "auto"),
                         false);
  }

  const uint64_t before_minute = runtime->last_clock_minute;
  update_clock(*runtime, now_ms);
  if (runtime->last_clock_minute != before_minute && runtime->initialized) {
    (void)render_current(*runtime, now_ms, "clock", false);
  }
}

}  // namespace

bool start(const app_core::AppSnapshot& snapshot,
           const app_core::RtcDateTime& clock, bool rtc_fallback) {
  if (g_runtime.timer != nullptr) return false;
  g_runtime.snapshot = snapshot;
  g_runtime.initial_clock = clock;
  g_runtime.rtc_fallback = rtc_fallback;
  g_runtime.initialized = false;
  g_runtime.active_pages.clear();
  g_runtime.cycle = 0;
  if (!board::lvgl_lock(1000)) {
    ESP_LOGE(kTag, "fatal: unable to acquire LVGL lock for UI timer");
    return false;
  }
  g_runtime.timer = lv_timer_create(timer_callback, kTimerPeriodMs, &g_runtime);
  board::lvgl_unlock();
  if (g_runtime.timer == nullptr) {
    ESP_LOGE(kTag, "fatal: unable to create 100 ms UI timer");
    return false;
  }
  ESP_LOGI(kTag, "UI timer started period_ms=%u", kTimerPeriodMs);
  return true;
}

}  // namespace ui
