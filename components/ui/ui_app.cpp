#include "ui_app.hpp"

#include "carousel_controller.hpp"
#include "now_playing_controller.hpp"

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
#include "ota_confirm.hpp"
#ifndef NDEBUG
#endif
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
  std::vector<app_core::PageKey> active_pages;
  app_core::CarouselState carousel;
  // Now-playing seize and volume-overlay state machines (Task 4). Ticked
  // every timer_callback regardless of which page is showing - see the call
  // site for why - so both live here beside carousel rather than only being
  // constructed when the now-playing page happens to be on screen.
  app_core::SeizeState now_playing_seize;
  app_core::VolumeOverlayState volume_overlay;
  // Mirrors showing_setup/showing_ota/showing_settings below: true only once
  // a seize has actually rendered PageId::NowPlaying, so the rebuild-gating
  // comparison at the call site can tell "already showing it" from "about to
  // take over this tick" without re-deriving it from now_playing_seize.
  bool showing_now_playing = false;
  // Last observed media-session state, so a session opening or closing can
  // rebuild the rotation immediately - see timer_callback for why waiting
  // for the carousel to wrap is not good enough.
  bool last_session_open = false;
  // The whole-second the now-playing page last drew. m:ss only changes once a
  // second, and the progress bar moves about 1.6 px/s on a 388 px span over a
  // four-minute track, so a finer interval would repaint the panel for a
  // difference nobody can see.
  uint32_t last_now_playing_second = 0;
  lv_timer_t* timer = nullptr;
  uint32_t cycle = 0;
  bool initialized = false;
  // Same role as showing_setup below, for the OTA takeover page.
  bool showing_ota = false;
  // Same role as showing_setup, for the settings menu.
  bool showing_settings = false;
  SettingsMenu settings;
  // Tracks whether the Setup page (rather than a carousel page) is the last
  // thing rendered, so entering/leaving setup mode triggers exactly one
  // atomic page replacement instead of one every 100 ms tick.
  bool showing_setup = false;
};

constexpr app_core::PageKey kHomeKey{app_core::PageId::Home, 0};

Runtime g_runtime;

// Mutex-guarded handoff from any FreeRTOS task (publish_snapshot) to the
// LVGL-thread timer (consume_published). No lv_* call ever happens while
// holding this mutex. Created in start(), matching lvgl_port's
// lazy-in-init-function mutex pattern rather than a global-constructor-time
// heap allocation.
SemaphoreHandle_t g_publish_mutex = nullptr;
app_core::AppSnapshot g_published_snapshot;
bool g_published_dirty = false;
std::string g_pending_update_status;
bool g_pending_update_status_dirty = false;
bool g_pending_update_available = false;
#ifndef NDEBUG
// Same mutex-guarded handoff as g_pending_update_status_dirty above, for
// GET /dither-card (wifi_provision/portal.cpp) requesting the debug test
// card - a one-shot signal, cleared the tick it is observed.
bool g_dither_card_requested = false;
// LVGL-thread-only, unlike the flag above: never touched under
// g_publish_mutex. Sticky once true - there is no route back to the
// carousel short of POST /restart, on purpose (see timer_callback's own
// comment on why this is reasserted every tick rather than loaded once).
// g_dither_card_screen is built once, on the first request, and reused
// after.
bool g_dither_card_active = false;
lv_obj_t* g_dither_card_screen = nullptr;
#endif

void (*g_setup_gesture_handler)() = nullptr;
// Runs the release check off the LVGL thread; the result comes back through
// set_update_status. Null until main registers it, in which case selecting the
// row simply reports nothing rather than blocking the render loop on a network
// call.
void (*g_update_handler)(bool install) = nullptr;
// Null until main registers it (see set_volume_changed_handler's own
// comment for why this is separate from the preset's store handler), in
// which case cycling the Volume row simply reports nothing rather than
// crashing on a null call.
void (*g_volume_changed_handler)() = nullptr;

// The subset of AppSnapshot that non-UI FreeRTOS tasks (wifi_provision, the
// battery monitor, and now the indoor/weather/market/net_time providers)
// publish. clock is included too - update_clock() below defers to it once
// its source reads "SNTP" instead of recomputing from the boot-time RTC/
// fallback clock.
struct PublishedFields {
  app_core::OtaData ota;
  app_core::SetupData setup;
  app_core::BatteryData battery;
  // Its own field, not part of BatteryData above, for the same reason
  // AppSnapshot::battery_runtime is its own field rather than
  // BatteryData::runtime - see that comment in app_snapshot.hpp.
  app_core::RuntimeEstimate battery_runtime;
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
    out.ota = g_published_snapshot.ota;
    out.setup = g_published_snapshot.setup;
    out.battery = g_published_snapshot.battery;
    out.battery_runtime = g_published_snapshot.battery_runtime;
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
    case app_core::PageId::Settings:
      return "Settings";
    case app_core::PageId::Ota:
      return "Ota";
    case app_core::PageId::NowPlaying:
      return "NowPlaying";
    case app_core::PageId::AssistantCard:
      return "AssistantCard";
  }
  return "Unknown";
}

uint8_t dwell_for(const Runtime& runtime, app_core::PageKey page) {
  for (const auto& descriptor : runtime.registry.descriptors()) {
    if (descriptor.key == page) return descriptor.dwell_seconds;
  }
  return page.id == app_core::PageId::Home ? 30 : 12;
}

void rebuild_active_pages(Runtime& runtime) {
  runtime.active_pages = runtime.registry.page_keys();
  if (runtime.active_pages.empty()) {
    runtime.active_pages.push_back(kHomeKey);
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

bool remove_failed_page(Runtime& runtime, app_core::PageKey page) {
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
  const app_core::PageKey page =
      runtime.active_pages.empty()
          ? kHomeKey
          : runtime.active_pages[std::min(runtime.carousel.index,
                                          runtime.active_pages.size() - 1)];
  // The slot is logged beside the name because a multi-slot page - the
  // assistant's cards - is otherwise six identical lines in the log.
  ESP_LOGI(kTag,
           "transition cycle=%" PRIu32 " page=%s/%u reason=%s dwell_s=%u "
           "manual_until_ms=%" PRIu64,
           runtime.cycle, page_name(page.id), page.slot, reason,
           dwell_for(runtime, page), runtime.carousel.manual_until_ms);
}

bool render_current(Runtime& runtime, const char* reason, bool show_overlay) {
  for (std::size_t attempt = 0; attempt < runtime.active_pages.size() + 1;
       ++attempt) {
    if (runtime.active_pages.empty()) return false;
    runtime.carousel.index =
        std::min(runtime.carousel.index, runtime.active_pages.size() - 1);
    const app_core::PageKey page = runtime.active_pages[runtime.carousel.index];
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

    ESP_LOGE(kTag, "renderer failure page=%s/%u; skipping for current cycle",
             page_name(page.id), page.slot);
    if (!remove_failed_page(runtime, page)) {
      runtime.active_pages.clear();
      runtime.active_pages.push_back(kHomeKey);
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


  // Set when the settings page needs redrawing - the cursor moved, a value
  // changed, or it was just opened.
  bool settings_dirty = false;
  SettingsAction settings_action = SettingsAction::None;

  // Drained here rather than written directly by the checking task: this is
  // the LVGL thread, and settings_status is read while rendering.
  if (g_publish_mutex != nullptr &&
      xSemaphoreTake(g_publish_mutex, 0) == pdTRUE) {
    if (g_pending_update_status_dirty) {
      runtime->context.settings_status = g_pending_update_status;
      runtime->settings.set_update_offered(g_pending_update_available);
      g_pending_update_status_dirty = false;
      settings_dirty = true;
    }
#ifndef NDEBUG
    if (g_dither_card_requested) {
      // Sticky, not one-shot: see the block below for why this is asserted
      // every tick rather than loaded once and left alone.
      g_dither_card_active = true;
      g_dither_card_requested = false;
    }
#endif
    xSemaphoreGive(g_publish_mutex);
  }

#ifndef NDEBUG
  // Owns the display until POST /restart, the same way OTA/Setup/Settings
  // own it below for their own reasons - a test card that loses a race
  // with the carousel is not usable for the thing it exists for.
  //
  // Built once and cached; every tick after that just reloads the same
  // screen, which is the fix, not a redundant safety net: a first version
  // of this feature loaded the screen exactly once, on the assumption that
  // only the display's active screen ever reaches the panel (true of
  // LVGL's own invalidation/redraw path - checked directly against
  // lv_obj_area_is_visible() in LVGL's own source, which skips any object
  // whose screen is neither the active one nor the previous one) - and a
  // real board still showed the carousel again minutes later regardless.
  // Rather than keep chasing which specific call reactivates it, this
  // makes the outcome unconditional instead: whatever ran between two
  // ticks, this tick puts the card back in front within one ~100 ms
  // period. lv_screen_load() is a single pointer comparison and an early
  // return when the screen it is given is already active, so reasserting
  // it here every tick costs nothing on all the ticks it was not actually
  // needed.
  if (g_dither_card_active) {
    if (g_dither_card_screen == nullptr) {
      g_dither_card_screen = build_dither_card_screen();
    }
    if (g_dither_card_screen != nullptr) {
      lv_scr_load(g_dither_card_screen);
    } else {
      ESP_LOGE(kTag, "dither card: screen build failed");
    }
    return;
  }
#endif

  PublishedFields published;
  const bool published_updated = consume_published(published);
  // Compared before the merge below overwrites it. Unlike the four provider
  // fields, OTA state has to reach the screen the moment it changes - a
  // percentage that only repaints when the carousel next happens to move
  // would sit frozen for the entire write, which is precisely when someone is
  // watching the panel to decide whether it is safe to unplug.
  //
  // Known limitation: this is a full page rebuild per change, throttled only
  // by the publisher (ota::Session republishes on whole-percent steps). If
  // those repaints prove visible on the panel, register the percent label in
  // UiContext and extend the label-only path to cover it instead.
  const bool ota_changed =
      published_updated &&
      (published.ota.phase != runtime->snapshot.ota.phase ||
       published.ota.percent != runtime->snapshot.ota.percent ||
       published.ota.percent_known != runtime->snapshot.ota.percent_known ||
       published.ota.detail != runtime->snapshot.ota.detail);
  if (published_updated) {
    runtime->snapshot.ota = published.ota;
    runtime->snapshot.setup = published.setup;
    runtime->snapshot.battery = published.battery;
    runtime->snapshot.battery_runtime = published.battery_runtime;
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
    // No tray-indicator field here (there used to be one, tray_activity):
    // that state does not travel through the AppSnapshot publish/consume
    // pipeline at all any more. It goes through app_core's tray registry
    // directly - see tray_registry.hpp and update_visible_fields() in
    // render_shared.cpp, which read it fresh every tick regardless of
    // whether a publish happened this cycle.
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
      // The confirm prompt takes the buttons before anything else can: while
      // it is up they mean yes and no, and the screen says so.
      if (app_core::ota_awaits_confirm(runtime->snapshot.ota)) {
        if (event == board::ButtonEvent::Next) {
          ESP_LOGI(kTag, "button event=BOOT action=accept-update");
          ota::answer_confirm(true);
        } else if (event == board::ButtonEvent::Previous) {
          ESP_LOGI(kTag, "button event=KEY action=reject-update");
          ota::answer_confirm(false);
        }
        continue;
      }
      // Checked ahead of the setup gesture, which is handled before the
      // navigation guard below and would otherwise tear the screen away from
      // an in-progress write and start an AP while it runs.
      if (app_core::ota_owns_screen(runtime->snapshot.ota)) continue;
      // While the menu owns the screen the two buttons mean something else
      // entirely, so this block comes first and consumes every event: KEY
      // moves the cursor, BOOT selects, and a KEY long press leaves. The
      // bottom band on that page says exactly this, which is the only reason
      // changing the meaning of a button is defensible at all.
      if (runtime->showing_settings) {
        if (event == board::ButtonEvent::EnterSetup) {
          ESP_LOGI(kTag, "button event=KEY-LONG action=leave-settings");
          runtime->showing_settings = false;
          runtime->carousel.page_started_ms = now_ms;
          (void)render_current(*runtime, "settings-exit", false);
          page_rebuilt = true;
        } else if (event == board::ButtonEvent::Previous) {
          runtime->settings.focus_next();
          runtime->context.settings_focus = runtime->settings.focused_index();
          settings_dirty = true;
        } else if (event == board::ButtonEvent::Next) {
          settings_action = runtime->settings.activate();
          settings_dirty = true;
        }
        continue;
      }
      if (event == board::ButtonEvent::OpenMenu) {
        ESP_LOGI(kTag, "button event=BOOT-LONG action=open-settings");
        runtime->settings.reset();
        runtime->context.settings_focus = 0;
        runtime->context.settings_status.clear();
        runtime->showing_settings = true;
        settings_dirty = true;
        continue;
      }
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
      // suspended until snapshot.setup.active goes false again. OTA outranks
      // it: while flash is being written there is no page worth navigating to
      // and the write must not be given a reason to share the LVGL thread.
      if (app_core::ota_owns_screen(runtime->snapshot.ota)) continue;
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

  // Both machines run every tick regardless of which page is up: the seize
  // has to notice a session opening while another page is showing, and the
  // overlay's own timer has to expire even on the tick where nothing else
  // changed.
  const app_core::NowPlaying media = app_core::now_playing();

  // A session opening or closing changes which pages exist, and the registry
  // is only consulted by begin_cycle(). Left to the carousel's own wrap, that
  // is a two-minute wait for a page the operator expects to see immediately:
  // the seize holds the screen for 60 s while stamping page_started_ms every
  // tick, so rotation does not advance at all during the hold, and only then
  // does a full five-page lap have to elapse before the registry is asked
  // again. Rebuilding here makes it one tick.
  //
  // begin_cycle() resets the index to 0. On open that is invisible, because
  // the seize below takes the screen on this same tick. On close it is a
  // deliberate return to the top of the rotation, which is also when the page
  // has to leave it.
  if (media.session_open != runtime->last_session_open) {
    runtime->last_session_open = media.session_open;
    begin_cycle(*runtime, now_ms);
  }

  const app_core::SeizeState previous_seize = runtime->now_playing_seize;
  runtime->now_playing_seize = app_core::seize_tick(
      previous_seize, media.session_open, media.title, now_ms);

  // The four pages that own the screen for a reason of their own, in one
  // named place because both the seize below and the carousel block further
  // down have to respect exactly the same set - and did not, at first. An
  // earlier version of this block returned before these were ever consulted,
  // which let a media session take the screen away from an OTA write in
  // progress: the panel would show a track title while the firmware was
  // writing its own flash, with nothing left on screen telling anyone not to
  // pull the power.
  //
  // The state machines above still tick through all of this. Only taking the
  // screen is suppressed, so an OTA that finishes inside the 60 s hold hands
  // back to a seize that has been counting down the whole time, rather than
  // to one that starts over.
  const bool takeover_page_owns_screen =
      runtime->snapshot.setup.active || runtime->showing_settings ||
      app_core::ota_owns_screen(runtime->snapshot.ota) ||
      app_core::ota_awaits_confirm(runtime->snapshot.ota);

  const bool page_is_now_playing =
      !takeover_page_owns_screen &&
      (runtime->now_playing_seize.owns_screen ||
       (!runtime->active_pages.empty() &&
        runtime->active_pages[runtime->carousel.index].id ==
            app_core::PageId::NowPlaying));

  const bool overlay_was_visible = runtime->volume_overlay.visible;
  runtime->volume_overlay = app_core::volume_overlay_tick(
      runtime->volume_overlay, media.volume, page_is_now_playing, now_ms);
  runtime->context.volume_overlay_visible = runtime->volume_overlay.visible;

  const bool now_playing_owns_screen =
      runtime->now_playing_seize.owns_screen && !takeover_page_owns_screen;

  if (now_playing_owns_screen) {
    // Rebuild only on a real transition - taking the screen, changing track,
    // or the overlay appearing or disappearing. A rebuild every tick would
    // repaint the whole reflective panel ten times a second, which is exactly
    // what update_visible_fields() exists to avoid.
    const bool seize_changed =
        !previous_seize.owns_screen ||
        previous_seize.title != runtime->now_playing_seize.title;
    // Without this the bar and the times froze for the entire 60 s hold: the
    // gating had no case for a progress change, so nothing repainted between
    // taking the screen and changing track. Reported from the panel as
    // "畫面更新可以勤一點", which was the polite version of "the progress
    // bar does not move".
    //
    // One repaint per second, not per tick. A tick is 100 ms and a full page
    // rebuild costs ~28 ms of conversion plus ~10.5 ms blocked on the panel's
    // DMA, so repainting on every tick would spend a third of the CPU redrawing
    // a bar that had moved a sixth of a pixel. If one per second still proves
    // to disturb the audio path, the next step is label-only updates through
    // update_visible_fields() rather than a full rebuild - the mechanism
    // already exists, it just needs this page's labels registered.
    const uint32_t now_playing_second = media.elapsed_ms / 1000;
    const bool second_changed =
        now_playing_second != runtime->last_now_playing_second;
    if (!runtime->showing_now_playing || seize_changed || second_changed ||
        overlay_was_visible != runtime->volume_overlay.visible) {
      runtime->last_now_playing_second = now_playing_second;
      const lv_obj_t* rendered =
          render_page(runtime->context, runtime->snapshot,
                      app_core::PageKey{app_core::PageId::NowPlaying, 0}, safe_canvas(), 0, 0);
      if (rendered == nullptr) ESP_LOGE(kTag, "renderer failure page=NowPlaying");
      page_rebuilt = true;
    }
    runtime->showing_now_playing = true;
    // Keeps the carousel's dwell timer from expiring behind the seize, so
    // releasing lands on a fresh dwell rather than an already-stale one.
    runtime->carousel.page_started_ms = now_ms;
  } else if (runtime->showing_now_playing) {
    // The release tick, and it has to repaint explicitly - exactly like
    // ota-exit, setup-exit and settings-exit below and above. Letting the
    // carousel block do it does not work: page_started_ms was stamped to
    // now_ms on every tick of the hold, so on this tick the dwell has run
    // for ~100 ms, carousel::tick reports no change, and nothing renders.
    // The panel would sit on the last now-playing frame until the residual
    // dwell elapsed and then jump to the *next* page, silently skipping
    // whichever page the carousel was actually on when the session started.
    // The flag clears either way, but the repaint is only worth doing when
    // the hold ended on its own. If a takeover page grabbed the screen
    // mid-hold, its own block a few lines down renders this same tick, and
    // painting the carousel first would be two full-panel repaints inside
    // one 100 ms tick - the exact cost every other piece of gating in this
    // function exists to avoid.
    runtime->showing_now_playing = false;
    if (!takeover_page_owns_screen) {
      runtime->carousel.page_started_ms = now_ms;
      (void)render_current(*runtime, "now-playing-exit", false);
      page_rebuilt = true;
    }
  }

  // No early return above, unlike a first version of this block. Everything
  // below - the carousel, the takeover pages, and the clock/tray updates at
  // the end of this function - still has to run while the seize holds the
  // screen. Returning skipped update_clock() and update_tray_indicators()
  // for the whole 60 s hold, which froze the clock on a page that does carry
  // the tray (page_shows_tray excludes only Ota) and reintroduced, for a full
  // minute, precisely the missed-indicator-blip failure update_tray_indicators'
  // own comment in render_shared.cpp exists to document. The takeover blocks
  // below are self-guarding: while the seize holds the screen none of
  // setup.active, showing_settings or the two ota_* predicates can be true,
  // because takeover_page_owns_screen is what let the seize run at all.
  if (!takeover_page_owns_screen && !now_playing_owns_screen) {
    const app_core::PageKey current_page =
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
    if (transition.page_changed ||
        (current_page.id == app_core::PageId::NowPlaying &&
         overlay_was_visible != runtime->volume_overlay.visible)) {
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
  // Ahead of the Setup block: OTA outranks it, so entering a write while
  // Setup happens to be up replaces the screen rather than being ignored.
  // Rebuilt on every OTA change, not just on entry, because the percentage is
  // the whole point of the page (see ota_changed above).
  if (app_core::ota_owns_screen(runtime->snapshot.ota) ||
      app_core::ota_awaits_confirm(runtime->snapshot.ota)) {
    if (!runtime->showing_ota || ota_changed) {
      const lv_obj_t* rendered =
          render_page(runtime->context, runtime->snapshot,
                      app_core::PageKey{app_core::PageId::Ota, 0}, safe_canvas(), 0, 0);
      if (rendered == nullptr) ESP_LOGE(kTag, "renderer failure page=Ota");
#ifndef NDEBUG
      // This page takes the screen and returns early, so it never reached the
      // carousel's own arming below - the one screen that most wants looking
#endif
    }
    runtime->showing_ota = true;
    // Not setup.active: Setup may still be logically active underneath, but it
    // is not what is on the screen. Leaving this true would make the Setup
    // block below skip its rebuild once the write finishes, stranding the OTA
    // page on a board that thinks it is showing Setup.
    runtime->showing_setup = false;
    // Returns before the clock and label-only paths: this page carries no
    // tray, so there is nothing for them to update.
    return;
  }
  if (runtime->showing_ota) {
    // The write is over. Land back on the carousel the same way leaving Setup
    // does, with the dwell timer restarted from this instant.
    runtime->showing_ota = false;
    runtime->carousel.page_started_ms = now_ms;
    (void)render_current(*runtime, "ota-exit", false);
    page_rebuilt = true;
  }

  if (settings_action == SettingsAction::VolumeChanged) {
    if (g_volume_changed_handler != nullptr) g_volume_changed_handler();
  } else if (settings_action == SettingsAction::StartUpdateCheck) {
    runtime->context.settings_status = text(Text::SettingsChecking);
    if (g_update_handler != nullptr) g_update_handler(false);
  } else if (settings_action == SettingsAction::StartUpdateInstall) {
    // No confirmation prompt here, unlike a push from the network. That prompt
    // exists because a push has no other authorisation - anyone on the LAN can
    // send one. This install was started by someone holding the board and
    //选ing the row; asking them to confirm the button they just pressed is a
    // second press for no added assurance.
    runtime->context.settings_status = text(Text::SettingsChecking);
    runtime->settings.set_update_offered(false);
    if (g_update_handler != nullptr) g_update_handler(true);
  } else if (settings_action == SettingsAction::EnterWifiSetup) {
    // Leaves the menu first: Wi-Fi setup is its own page, and the two must not
    // both believe they own the screen.
    runtime->showing_settings = false;
    settings_dirty = false;
    if (g_setup_gesture_handler != nullptr) g_setup_gesture_handler();
  }

  if (runtime->showing_settings) {
    if (settings_dirty) {
      const lv_obj_t* rendered =
          render_page(runtime->context, runtime->snapshot,
                      app_core::PageKey{app_core::PageId::Settings, 0}, safe_canvas(), 0, 0);
      if (rendered == nullptr) ESP_LOGE(kTag, "renderer failure page=Settings");
    }
    // Returns before the tray and label paths: this page carries neither.
    runtime->showing_setup = false;
    return;
  }

  if (runtime->snapshot.setup.active) {
    if (!runtime->showing_setup) {
      const lv_obj_t* rendered = render_page(
          runtime->context, runtime->snapshot,
          app_core::PageKey{app_core::PageId::Setup, 0}, safe_canvas(), 0, 0);
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

  // Every tick, unconditionally - not folded into the block above. A tray
  // indicator can go active and inactive again entirely within the gap
  // between two published/clock-minute events (a several-second tone is
  // shorter than either), and update_visible_fields' own gating would miss
  // it silently if this shared that condition. See update_tray_indicators'
  // comment in render_shared.cpp for the hardware failure this fixes.
  if (runtime->initialized) {
    (void)update_tray_indicators(runtime->context);
  }
}

}  // namespace

bool start(const app_core::AppSnapshot& snapshot,
           const app_core::RtcDateTime& clock, bool rtc_fallback) {
  if (g_runtime.timer != nullptr) return false;
  // Before anything renders: until this runs the interface fonts carry no
  // Chinese fallback, so a first frame drawn ahead of it would show boxes.
  fonts_init();
  app_core::register_builtin_pages();
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

void set_update_handler(void (*handler)(bool install)) {
  g_update_handler = handler;
}

void set_volume_changed_handler(void (*handler)()) {
  g_volume_changed_handler = handler;
}

void set_update_status(const std::string& status, bool install_available) {
  // Written from whatever task ran the check. It is read on the LVGL thread on
  // the next tick, and a torn read of a std::string would be a crash, so the
  // publish goes through the same mutex the snapshot handoff uses.
  if (g_publish_mutex == nullptr) return;
  if (xSemaphoreTake(g_publish_mutex, portMAX_DELAY) != pdTRUE) return;
  g_pending_update_status = status;
  g_pending_update_available = install_available;
  g_pending_update_status_dirty = true;
  xSemaphoreGive(g_publish_mutex);
}

#ifndef NDEBUG
void request_dither_card() {
  // No payload to carry, just a request - a plain bool under the same
  // mutex as every other cross-task flag here, not a new lock of its own.
  if (g_publish_mutex == nullptr) return;
  if (xSemaphoreTake(g_publish_mutex, portMAX_DELAY) != pdTRUE) return;
  g_dither_card_requested = true;
  xSemaphoreGive(g_publish_mutex);
}
#endif

}  // namespace ui
