#pragma once

#include "app_snapshot.hpp"
#include "ui_theme.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>

namespace ui {

struct ChartPoint {
  int x = 0;
  int y = 0;
};

struct MarketLayout {
  Rect primary;
  Rect side;
};

struct PageDotsGeometry {
  std::size_t count = 0;
  std::size_t active_index = 0;
  int start_x = 0;
  int total_width = 0;
  int y = 0;
};

inline constexpr int kPageDotSize = 5;
inline constexpr int kPageDotGap = 4;
inline constexpr char kComfortBandLabel[] = "COMFORT BAND  40-60 RH";

struct SetupLayout {
  Rect qr;
  Rect title;
  Rect ssid;
  Rect password;
  Rect portal;
  Rect instructions;
  Rect status;
};

// The QR is the dominant element on this reflective, backlight-less panel -
// module size drives scannability more than anything else. 200px (up from
// the original 132px) is height-constrained: it is what fits the
// tray-reduced content bounds while still leaving an explicit quiet-zone
// margin above/below (kSetupQrQuietMargin) and beside the text column
// (kSetupQrGap), per setup_layout_fits below.
inline constexpr int kSetupQrSize = 200;
inline constexpr int kSetupQrGap = 18;
inline constexpr int kSetupQrQuietMargin = 8;
inline constexpr int kSetupTitleHeight = 26;
inline constexpr int kSetupLineHeight = 18;
inline constexpr int kSetupLineGap = 4;
inline constexpr int kSetupTightLineGap = 2;
inline constexpr int kSetupBlockGap = 10;
inline constexpr int kSetupStatusGap = 6;
// A neutral status ("Connecting...", "Not yet connected") is one short line,
// but a failure status - a wrong-password message plus a next-step hint -
// needs to wrap. Sized for up to four wrapped small-font lines; the column
// has plenty of unused height below the instructions row (see
// setup_layout_fits below), so this only needs to be generously large, not
// an exact font-metric calculation.
inline constexpr int kSetupStatusHeight = 4 * kSetupLineHeight;
inline constexpr char kSetupTitle[] = "Setup";
inline constexpr char kSetupNoSsidLabel[] = "AP SSID unavailable";
inline constexpr char kSetupOpenPassword[] = "OPEN";
inline constexpr char kSetupInstructions[] =
    "Scan QR, join Wi-Fi, then open the setup page";
inline constexpr char kSetupDefaultStatus[] = "Not yet connected";
inline constexpr char kSetupQrUnavailableLabel[] =
    "QR unavailable - use SSID at left";

// QR sits flush against the right edge of bounds, vertically centered (the
// quiet zone this leaves above/below is proven by setup_qr_quiet_zone_ok
// below); the text column - title, then SSID/password/portal URL one per
// line, then instructions and status - fills the remaining width on the
// left, top-aligned. Pure arithmetic on bounds.x/y/width/height, so it works
// for both the zero-offset local frame render_setup receives and the
// absolute safe_canvas() frame host tests use.
constexpr SetupLayout setup_layout(const Rect bounds) {
  const Rect qr{bounds.right() - kSetupQrSize,
               bounds.y + (bounds.height - kSetupQrSize) / 2, kSetupQrSize,
               kSetupQrSize};
  const int text_width = bounds.width - kSetupQrSize - kSetupQrGap;
  const Rect title{bounds.x, bounds.y, text_width, kSetupTitleHeight};
  const Rect ssid{bounds.x, title.bottom() + kSetupLineGap, text_width,
                  kSetupLineHeight};
  const Rect password{bounds.x, ssid.bottom() + kSetupTightLineGap,
                      text_width, kSetupLineHeight};
  const Rect portal{bounds.x, password.bottom() + kSetupTightLineGap,
                    text_width, kSetupLineHeight};
  const Rect instructions{bounds.x, portal.bottom() + kSetupBlockGap,
                          text_width, kSetupLineHeight};
  const Rect status{bounds.x, instructions.bottom() + kSetupStatusGap,
                    text_width, kSetupStatusHeight};
  return {qr, title, ssid, password, portal, instructions, status};
}

// Every rect setup_layout produces fits entirely inside the given content
// bounds (not just the wider safe canvas) - the single source of truth the
// static_asserts below check against the tray-reduced Setup content area.
constexpr bool setup_layout_fits(const Rect content) {
  const SetupLayout layout = setup_layout(content);
  return rect_within(content, layout.qr) && rect_within(content, layout.title) &&
        rect_within(content, layout.ssid) &&
        rect_within(content, layout.password) &&
        rect_within(content, layout.portal) &&
        rect_within(content, layout.instructions) &&
        rect_within(content, layout.status);
}

// No two rects in the layout overlap, checked pairwise across all of them.
constexpr bool setup_layout_disjoint(const Rect content) {
  const SetupLayout layout = setup_layout(content);
  const std::array<Rect, 7> rects{layout.qr,           layout.title,
                                  layout.ssid,         layout.password,
                                  layout.portal,       layout.instructions,
                                  layout.status};
  for (std::size_t i = 0; i < rects.size(); ++i) {
    for (std::size_t j = i + 1; j < rects.size(); ++j) {
      if (rects_intersect(rects[i], rects[j])) return false;
    }
  }
  return true;
}

// The QR keeps at least kSetupQrQuietMargin of empty space above and below
// it within content, and exactly kSetupQrGap of empty space between it and
// the text column - a QR flush against other ink is much harder for a
// camera to lock onto, especially on a reflective, backlight-less panel.
constexpr bool setup_qr_quiet_zone_ok(const Rect content) {
  const SetupLayout layout = setup_layout(content);
  const bool vertical_margin_ok =
      (layout.qr.y - content.y) >= kSetupQrQuietMargin &&
      (content.bottom() - layout.qr.bottom()) >= kSetupQrQuietMargin;
  const bool text_gap_ok = (layout.qr.x - layout.title.right()) == kSetupQrGap &&
                           (layout.qr.x - layout.portal.right()) == kSetupQrGap;
  return vertical_margin_ok && text_gap_ok;
}

// Formats the SSID/passphrase/portal text block and the tray's live fields.
// Small, pure string helpers kept alongside the layout constants they pair
// with so renderers and the label-only repaint path (ui_app.cpp) share one
// source of truth instead of formatting the same text twice.
inline std::string setup_status_text(const std::string& status) {
  return status.empty() ? std::string(kSetupDefaultStatus) : status;
}

struct SystemTrayLayout {
  Rect time;
  Rect network;
  Rect battery;
};

// Sized to match the previous mast band (28) plus its trailing gap (8) so
// the reduced content area on data pages is pixel-identical to before -
// only the addition of an explicit separator changes that 8 into 1 + 7.
inline constexpr int kSystemTrayHeight = 28;
inline constexpr int kSystemTrayContentGap = 7;
inline constexpr int kSystemTrayReservedHeight =
    kSystemTrayHeight + kSeparatorWidth + kSystemTrayContentGap;
inline constexpr int kSystemTrayTimeWidth = 60;
inline constexpr int kSystemTrayTimeHeight = 25;
inline constexpr int kSystemTrayCellY = 3;
inline constexpr int kSystemTrayCellHeight = 18;
inline constexpr int kSystemTrayCellGap = 4;
inline constexpr int kSystemTrayBatteryWidth = 64;

// Time flush left, battery flush right, network status filling the middle.
// Pure arithmetic on bounds.x/y/width/height, so - like setup_layout - it
// works for both the zero-offset local frame render_page builds pages with
// and the absolute safe_canvas() frame the static_asserts below use.
constexpr SystemTrayLayout system_tray_layout(const Rect bounds) {
  const Rect time{bounds.x, bounds.y, kSystemTrayTimeWidth,
                  kSystemTrayTimeHeight};
  const Rect battery{bounds.right() - kSystemTrayBatteryWidth,
                     bounds.y + kSystemTrayCellY, kSystemTrayBatteryWidth,
                     kSystemTrayCellHeight};
  const int network_x = time.right() + kSystemTrayCellGap;
  const int network_width = battery.x - kSystemTrayCellGap - network_x;
  const Rect network{network_x, bounds.y + kSystemTrayCellY, network_width,
                     kSystemTrayCellHeight};
  return {time, network, battery};
}

// One of the three fixed tray network strings.
inline std::string tray_network_text(const app_core::SetupData& setup) {
  return setup.active ? "SETUP" : (setup.connected ? "WIFI" : "NO WIFI");
}

// Empty when unread/implausible (BatteryData::valid false) so the tray cell
// renders blank rather than a misleading "BAT 0%".
inline std::string tray_battery_text(const app_core::BatteryData& battery) {
  if (!battery.valid) return {};
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "BAT %u%%", battery.percent);
  return buffer;
}

// One place to decide which pages carry the tray. Home is deliberately
// excluded so the Clock Hero keeps the full canvas.
constexpr bool page_shows_tray(app_core::PageId page) {
  return page != app_core::PageId::Home;
}

// Every page derives its content area through this single path: a
// tray-less page (Home) gets the untouched bounds back, a tray page gets
// bounds with kSystemTrayReservedHeight sliced off the top. Bottom stays
// fixed either way, so page dots anchored to bounds.bottom() do not move.
constexpr Rect content_bounds(const Rect bounds, app_core::PageId page) {
  if (!page_shows_tray(page)) return bounds;
  return {bounds.x, bounds.y + kSystemTrayReservedHeight, bounds.width,
          bounds.height - kSystemTrayReservedHeight};
}

static_assert(system_tray_layout(safe_canvas()).time.right() <=
                  system_tray_layout(safe_canvas()).network.x,
              "tray time and network cells do not overlap");
static_assert(system_tray_layout(safe_canvas()).network.right() <=
                  system_tray_layout(safe_canvas()).battery.x,
              "tray network and battery cells do not overlap");
static_assert(system_tray_layout(safe_canvas()).network.width > 0,
              "tray network cell has positive width");
static_assert(system_tray_layout(safe_canvas()).battery.right() ==
                  safe_canvas().right(),
              "tray battery cell sits flush right");
static_assert(within_safe_canvas(system_tray_layout(safe_canvas()).time) &&
                  within_safe_canvas(system_tray_layout(safe_canvas()).network) &&
                  within_safe_canvas(system_tray_layout(safe_canvas()).battery),
              "tray cells stay inside the safe canvas");

static_assert(!page_shows_tray(app_core::PageId::Home),
              "home keeps the full canvas without a tray");
static_assert(page_shows_tray(app_core::PageId::TaiwanMarket) &&
                  page_shows_tray(app_core::PageId::UsMarket) &&
                  page_shows_tray(app_core::PageId::Weather) &&
                  page_shows_tray(app_core::PageId::Indoor) &&
                  page_shows_tray(app_core::PageId::Setup),
              "data pages and setup carry the tray");

static_assert(content_bounds(safe_canvas(), app_core::PageId::Home).x ==
                      safe_canvas().x &&
                  content_bounds(safe_canvas(), app_core::PageId::Home).y ==
                      safe_canvas().y &&
                  content_bounds(safe_canvas(), app_core::PageId::Home)
                          .width == safe_canvas().width &&
                  content_bounds(safe_canvas(), app_core::PageId::Home)
                          .height == safe_canvas().height,
              "home content bounds are the untouched safe canvas");
static_assert(
    content_bounds(safe_canvas(), app_core::PageId::Weather).y ==
        safe_canvas().y + kSystemTrayReservedHeight,
    "a tray page loses exactly the reserved tray height off the top");
static_assert(
    content_bounds(safe_canvas(), app_core::PageId::Weather).bottom() ==
        safe_canvas().bottom(),
    "a tray page keeps the same bottom edge, so page dots do not move");
static_assert(
    content_bounds(safe_canvas(), app_core::PageId::Weather).height > 0,
    "tray pages keep positive content height");
static_assert(
    within_safe_canvas(content_bounds(safe_canvas(), app_core::PageId::Weather)),
    "tray-reduced content bounds stay inside the safe canvas");

// Setup gets the tray too, so its (much larger) QR and text block must still
// fit once setup_layout is recomputed against the reduced content bounds -
// with no overlaps and the QR's quiet zone intact.
static_assert(
    setup_layout_fits(content_bounds(safe_canvas(), app_core::PageId::Setup)),
    "every setup rect fits entirely inside the tray-reduced content bounds");
static_assert(
    setup_layout_disjoint(
        content_bounds(safe_canvas(), app_core::PageId::Setup)),
    "no two setup rects overlap");
static_assert(
    setup_qr_quiet_zone_ok(
        content_bounds(safe_canvas(), app_core::PageId::Setup)),
    "setup QR keeps its quiet-zone margin from the content edges and the "
    "text column");
static_assert(
    setup_layout(content_bounds(safe_canvas(), app_core::PageId::Setup))
            .qr.width == kSetupQrSize &&
        kSetupQrSize > 132,
    "setup QR is enlarged well beyond the original 132px size");
static_assert(
    setup_layout(content_bounds(safe_canvas(), app_core::PageId::Setup))
            .status.height >= 3 * kSetupLineHeight,
    "setup status has room for a multi-line error message, not just one "
    "short line");

constexpr PageDotsGeometry page_dots_geometry(const Rect bounds,
                                              std::size_t page_index,
                                              std::size_t page_count) {
  if (page_count == 0) {
    return {0, 0, bounds.right(), 0, bounds.y + bounds.height - kPageDotSize};
  }
  const std::size_t active_index =
      page_index < page_count ? page_index : page_count - 1;
  const int total_width = static_cast<int>(
      page_count * kPageDotSize + (page_count - 1) * kPageDotGap);
  return {page_count, active_index, bounds.right() - total_width, total_width,
          bounds.y + bounds.height - kPageDotSize};
}

inline std::string format_minute_clock(std::string clock) {
  const std::size_t first = clock.find(':');
  if (first != std::string::npos) {
    const std::size_t second = clock.find(':', first + 1);
    if (second != std::string::npos) clock.resize(second);
  }
  return clock;
}

inline std::string compact_clock_source(const std::string& source) {
  if (source == "RTC fallback") return "DEMO / FALLBACK";
  if (source == "PCF85063") return "DEMO / RTC";
  return "DEMO / UNKNOWN";
}

constexpr MarketLayout market_layout(const Rect bounds) {
  constexpr int kPrimaryNumerator = 72;
  constexpr int kPercent = 100;
  const int primary_width =
      (bounds.width * kPrimaryNumerator) / kPercent;
  return {{bounds.x, bounds.y, primary_width, bounds.height},
          {bounds.x + primary_width + kSeparatorWidth, bounds.y,
           bounds.width - primary_width - kSeparatorWidth, bounds.height}};
}

constexpr std::array<Rect, 7> forecast_columns(const Rect bounds) {
  std::array<Rect, 7> columns{};
  const int column_width = bounds.width / 7;
  for (std::size_t index = 0; index < columns.size(); ++index) {
    const int x = bounds.x + static_cast<int>(index) * column_width;
    const int next_x = index + 1 == columns.size()
                           ? bounds.right()
                           : x + column_width;
    columns[index] = {x, bounds.y, next_x - x, bounds.height};
  }
  return columns;
}

constexpr std::array<ChartPoint, 8> normalize_chart_samples(
    const std::array<int, 8>& samples, const Rect bounds) {
  std::array<ChartPoint, 8> points{};
  int minimum = samples[0];
  int maximum = samples[0];
  for (const int sample : samples) {
    if (sample < minimum) minimum = sample;
    if (sample > maximum) maximum = sample;
  }
  const int range = maximum - minimum;
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const int x = bounds.x +
                  static_cast<int>((index * static_cast<std::size_t>(
                                        bounds.width - 1)) /
                                    (samples.size() - 1));
    int y = bounds.y + bounds.height / 2;
    if (range != 0) {
      const int from_top =
          ((maximum - samples[index]) * (bounds.height - 1)) / range;
      y = bounds.y + from_top;
    }
    if (y < bounds.y) y = bounds.y;
    if (y >= bounds.bottom()) y = bounds.bottom() - 1;
    points[index] = {x, y};
  }
  return points;
}

}  // namespace ui
