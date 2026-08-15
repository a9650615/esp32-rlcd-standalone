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
#include <freertos/semphr.h>
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
  // Tracks whether the Setup page (rather than a carousel page) is the last
  // thing rendered, so entering/leaving setup mode triggers exactly one
  // atomic page replacement instead of one every 100 ms tick.
  bool showing_setup = false;
};

Runtime g_runtime;

// Mutex-guarded handoff from any FreeRTOS task (publish_snapshot) to the
// LVGL-thread timer (consume_published). No lv_* call ever happens while
// holding this mutex. Created in start(), matching lvgl_port's
// lazy-in-init-function mutex pattern rather than a global-constructor-time
// heap allocation.
SemaphoreHandle_t g_publish_mutex = nullptr;
app_core::AppSnapshot g_published_snapshot;
bool g_published_dirty = false;

void (*g_setup_gesture_handler)() = nullptr;

// The subset of AppSnapshot that non-UI FreeRTOS tasks (wifi_provision, the
// battery monitor, and now the indoor/weather/market/net_time providers)
// publish. clock is included too - update_clock() below defers to it once
// its source reads "SNTP" instead of recomputing from the boot-time RTC/
// fallback clock.
struct PublishedFields {
  app_core::SetupData setup;
  app_core::BatteryData battery;
  app_core::IndoorData indoor;
  app_core::WeatherData weather;
  app_core::WeatherData new_york_weather;
  app_core::MarketData taiwan_market;
  app_core::MarketData us_market;
  app_core::ClockData clock;
};

// Non-blocking: if the mutex is momentarily held by a concurrent publish,
// this simply tries again on the next 100 ms tick.
bool consume_published(PublishedFields& out) {
  if (g_publish_mutex == nullptr) return false;
  if (xSemaphoreTake(g_publish_mutex, 0) != pdTRUE) return false;
  const bool dirty = g_published_dirty;
  if (dirty) {
    out.setup = g_published_snapshot.setup;
    out.battery = g_published_snapshot.battery;
    out.indoor = g_published_snapshot.indoor;
    out.weather = g_published_snapshot.weather;
    out.new_york_weather = g_published_snapshot.new_york_weather;
    out.taiwan_market = g_published_snapshot.taiwan_market;
    out.us_market = g_published_snapshot.us_market;
    out.clock = g_published_snapshot.clock;
    g_published_dirty = false;
  }
  xSemaphoreGive(g_publish_mutex);
  return dirty;
}

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
    case app_core::PageId::Setup:
      return "Setup";
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

app_core::RtcDateTime clock_at(const Runtime& runtime, uint64_t now_ms) {
  const uint64_t elapsed_seconds =
      now_ms >= runtime.started_ms ? (now_ms - runtime.started_ms) / 1000 : 0;
  return app_core::advance_rtc_datetime(runtime.initial_clock, elapsed_seconds);
}

const char* weekday_name(const app_core::RtcDateTime& date) {
  // 1 Jan 2000 was a Saturday. The short loop is only used once per minute.
  uint64_t days = 0;
  for (uint16_t year = 2000; year < date.year; ++year) {
    days += (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
  }
  for (uint8_t month = 1; month < date.month; ++month) {
    days += app_core::days_in_month(date.year, month);
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
  // Once net_time has published a real SNTP-synced clock (see
  // consume_published/PublishedFields above), that reading owns the display
  // - recomputing from the boot-time RTC/compile-time fallback clock here
  // would stamp over real network time every minute. Before the first sync
  // lands, snapshot.clock.source is whatever start() was given (empty or
  // "RTC fallback"/"PCF85063"), so the fallback path below still runs
  // exactly as before.
  if (runtime.snapshot.clock.source == "SNTP") return;
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

bool render_current(Runtime& runtime, const char* reason, bool show_overlay) {
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

bool initialize_runtime(Runtime& runtime, uint64_t now_ms) {
  runtime.started_ms = now_ms;
  runtime.last_clock_minute = std::numeric_limits<uint64_t>::max();
  update_clock(runtime, now_ms);
  lv_obj_t* host = lv_obj_create(lv_screen_active());
  if (host == nullptr || !init_context(runtime.context, host)) {
    ESP_LOGE(kTag, "fatal: UI host/context initialization failed");
    if (host != nullptr) lv_obj_delete(host);
    return false;
  }
  lv_obj_set_size(host, kCanvasWidth, kCanvasHeight);
  lv_obj_set_pos(host, 0, 0);
  apply_surface(host);
  begin_cycle(runtime, now_ms);
  runtime.carousel.index = 0;
  runtime.initialized = true;
  if (!render_current(runtime, "startup", false)) {
    ESP_LOGE(kTag, "fatal: initial page render failed");
    reset_context(runtime.context);
    runtime.initialized = false;
    return false;
  }
  return true;
}

void timer_callback(lv_timer_t* timer) {
  auto* runtime = static_cast<Runtime*>(lv_timer_get_user_data(timer));
  if (runtime == nullptr) return;
  const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);

  if (!runtime->initialized) return;

  PublishedFields published;
  const bool published_updated = consume_published(published);
  if (published_updated) {
    runtime->snapshot.setup = published.setup;
    runtime->snapshot.battery = published.battery;
    // indoor/weather/market/clock have no per-field widget registered the
    // way the tray clock/network/battery labels are (see UiContext) - a
    // structural change like NO DATA <-> real figures or a chart appearing
    // can't be a text-only diff anyway. So these are merged into the
    // snapshot here and simply picked up whole by the next render_current
    // this page already gets from normal carousel dwell/navigation, rather
    // than forcing an extra rebuild of whichever page happens to be showing
    // right now - that would be exactly the unconditional-rebuild-on-publish
    // flash/lockup risk the tray label-only path was built to avoid, now
    // multiplied by four independently-timed providers instead of one.
    // clock is the exception: update_visible_clock below already does a
    // label-only compare-and-set against runtime->snapshot.clock.hero, so
    // merging it here is all clock needs to reach the screen without a
    // rebuild.
    runtime->snapshot.indoor = published.indoor;
    runtime->snapshot.weather = published.weather;
    runtime->snapshot.new_york_weather = published.new_york_weather;
    runtime->snapshot.taiwan_market = published.taiwan_market;
    runtime->snapshot.us_market = published.us_market;
    runtime->snapshot.clock = published.clock;
  }

  // Set whenever a genuine page-identity change forces a full atomic
  // render_page/render_current rebuild this tick (entering/leaving Setup, or
  // a normal carousel transition). When that happens the fresh page root
  // already reflects the latest snapshot, so the label-only path below is
  // skipped entirely - a battery sample or status update on an otherwise
  // unchanged page must never cost a full repaint on this reflective panel.
  bool page_rebuilt = false;

  QueueHandle_t queue = board::button_event_queue();
  if (queue != nullptr) {
    board::ButtonEvent event;
    while (xQueueReceive(queue, &event, 0) == pdTRUE) {
      if (event == board::ButtonEvent::EnterSetup) {
        // Logged because this gesture was previously invisible: a KEY long
        // press that never armed and one whose handler was unregistered
        // looked identical from a serial capture, which is the whole
        // difference between a button problem and a wiring problem.
        ESP_LOGI(kTag, "button event=KEY-LONG handler=%s",
                 g_setup_gesture_handler != nullptr ? "registered" : "MISSING");
        if (g_setup_gesture_handler != nullptr) g_setup_gesture_handler();
        continue;
      }
      // The Setup page owns the screen while active; carousel navigation is
      // suspended until snapshot.setup.active goes false again.
      if (runtime->snapshot.setup.active) continue;
      const bool next = event == board::ButtonEvent::Next;
      const char* reason = next ? "manual-boot" : "manual-key";
      ESP_LOGI(kTag, "button event=%s", next ? "BOOT" : "KEY");
      const auto transition = next
                                  ? app_core::carousel::next(
                                        runtime->carousel, now_ms,
                                        runtime->active_pages.size())
                                  : app_core::carousel::previous(
                                        runtime->carousel, now_ms,
                                        runtime->active_pages.size());
      runtime->carousel = transition.state;
      if (transition.page_changed) {
        (void)render_current(*runtime, reason, true);
        page_rebuilt = true;
      }
    }
  }

  if (!runtime->snapshot.setup.active) {
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
      // Automatic dwell only: KEY/BOOT navigation above goes through
      // carousel::next/previous, which never sees the snapshot and so cannot
      // land anywhere but exactly where the button pointed. This only steers
      // unattended auto-advance past a page with nothing to show right now
      // (invalid data) or a market page on a Taipei-local weekend - see
      // page_relevant_for_auto_rotation. A no-op when the landed page is
      // already relevant, including every time begin_cycle just reset to
      // Home above.
      runtime->carousel.index = app_core::next_relevant_auto_index(
          runtime->active_pages, runtime->carousel.index, runtime->snapshot);
      (void)render_current(*runtime,
                           wrapped ? "cycle" :
                                      (manual_timeout ? "manual-timeout" : "auto"),
                           false);
      page_rebuilt = true;
    }
  }

  // Entering/leaving Setup is the one Setup-related event that still gets a
  // full atomic page-replacement (render_page via render_current/render_page
  // directly): the page identity itself changed. Any other Setup-active
  // publish (a status update, a battery sample) falls through to the
  // label-only path below instead of rebuilding.
  if (runtime->snapshot.setup.active) {
    if (!runtime->showing_setup) {
      const lv_obj_t* rendered = render_page(
          runtime->context, runtime->snapshot, app_core::PageId::Setup,
          safe_canvas(), 0, 0);
      if (rendered == nullptr) {
        ESP_LOGE(kTag, "renderer failure page=Setup");
      }
      page_rebuilt = true;
    }
  } else if (runtime->showing_setup) {
    // Resume exactly where a normal page transition would land: same
    // carousel index, dwell timer restarted fresh from this instant.
    runtime->carousel.page_started_ms = now_ms;
    (void)render_current(*runtime, "setup-exit", false);
    page_rebuilt = true;
  }
  runtime->showing_setup = runtime->snapshot.setup.active;

  const uint64_t before_minute = runtime->last_clock_minute;
  update_clock(*runtime, now_ms);
  const bool clock_minute_changed = runtime->last_clock_minute != before_minute;

  // Label-only repaint path: a page-identity change already redrew
  // everything this tick (page_rebuilt), so only reach here otherwise. Any
  // other publish (Setup status, battery, network) or a minute rollover
  // updates just the label(s) whose text differs - see
  // update_visible_fields/set_label_text_if_changed - never a page rebuild.
  if (!page_rebuilt && runtime->initialized &&
      (published_updated || clock_minute_changed)) {
    (void)update_visible_fields(runtime->context, runtime->snapshot);
  }
}

}  // namespace

bool start(const app_core::AppSnapshot& snapshot,
           const app_core::RtcDateTime& clock, bool rtc_fallback) {
  if (g_runtime.timer != nullptr) return false;
  ESP_LOGI(kTag, "main task stack free before UI init=%u bytes",
           static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  g_runtime.snapshot = snapshot;
  g_runtime.initial_clock = clock;
  g_runtime.rtc_fallback = rtc_fallback;
  g_runtime.initialized = false;
  g_runtime.active_pages.clear();
  g_runtime.cycle = 0;
  g_runtime.showing_setup = false;
  if (g_publish_mutex == nullptr) g_publish_mutex = xSemaphoreCreateMutex();
  if (!board::lvgl_lock(1000)) {
    ESP_LOGE(kTag, "fatal: unable to acquire LVGL lock for UI timer");
    return false;
  }
  const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
  if (!initialize_runtime(g_runtime, now_ms)) {
    board::lvgl_unlock();
    return false;
  }
  ESP_LOGI(kTag, "main task stack free after first render=%u bytes",
           static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  g_runtime.timer = lv_timer_create(timer_callback, kTimerPeriodMs, &g_runtime);
  if (g_runtime.timer == nullptr) {
    ESP_LOGE(kTag, "fatal: unable to create 100 ms UI timer");
    reset_context(g_runtime.context);
    g_runtime.initialized = false;
  }
  board::lvgl_unlock();
  if (g_runtime.timer == nullptr) {
    return false;
  }
  ESP_LOGI(kTag, "UI timer started period_ms=%u", kTimerPeriodMs);
  return true;
}

void publish_snapshot(const app_core::AppSnapshot& snapshot) {
  if (g_publish_mutex == nullptr) return;
  if (xSemaphoreTake(g_publish_mutex, portMAX_DELAY) != pdTRUE) return;
  g_published_snapshot = snapshot;
  g_published_dirty = true;
  xSemaphoreGive(g_publish_mutex);
}

void set_setup_gesture_handler(void (*handler)()) {
  g_setup_gesture_handler = handler;
}

}  // namespace ui
