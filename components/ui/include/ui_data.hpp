#pragma once

#include "app_snapshot.hpp"
#include "settings_menu.hpp"
#include "ui_strings.hpp"
#include "carousel_controller.hpp"
#include "ui_theme.hpp"

#include <algorithm>
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

// Collapses weather_parse.cpp's WMO condition strings (Clear, Mostly Clear,
// Partly Cloudy, Overcast, Fog, Drizzle/Icy Drizzle, Rain/Icy Rain, Showers,
// Snow/Snow Grains/Snow Showers, Thunderstorm/Tstorm Hail, Unknown - plus the
// mock fixture's Sunny/Cloudy/Storm) down to the four silhouettes
// weather_icon (ui_theme.cpp) can draw boldly enough to read on this panel.
// Substring matching, not an exhaustive switch, so it also covers whatever
// exact wording a future WMO code addition uses without a matching update
// here. Unmatched/empty text is treated as Cloud, the least specific claim.
inline WeatherIconKind weather_icon_kind_for_condition(
    const std::string& condition) {
  if (condition.find("Snow") != std::string::npos) return WeatherIconKind::Snow;
  if (condition.find("Rain") != std::string::npos ||
      condition.find("Drizzle") != std::string::npos ||
      condition.find("Shower") != std::string::npos ||
      // "torm" (no leading S/s) is deliberate: it matches "Storm",
      // "Thunderstorm" and WMO's "Tstorm Hail" regardless of whether the
      // storm syllable happens to be capitalized in that particular string.
      condition.find("torm") != std::string::npos) {
    return WeatherIconKind::Rain;
  }
  if (condition.find("Clear") != std::string::npos ||
      condition.find("Sunny") != std::string::npos) {
    return WeatherIconKind::Sun;
  }
  return WeatherIconKind::Cloud;
}

struct OtaLayout {
  Rect phase;
  Rect percent;
  Rect warning;
  Rect detail;
};

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
// Line heights for the two fonts render_setup.cpp uses, duplicated here as
// literals (see lv_font_montserrat_{14,20}.c) because this header is also
// compiled LVGL-free for host tests (UI_THEME_GEOMETRY_ONLY) and so cannot
// read font->line_height directly. Feeding these into safe_text_box_height
// below - the same helper label() applies at render time - is what keeps
// every reserved row height in sync with what LVGL will actually draw; see
// the runtime-growth defect fixed by commit b52ff70.
inline constexpr int kSetupSmallFontLineHeight = 16;   // lv_font_montserrat_14
inline constexpr int kSetupMediumFontLineHeight = 22;  // lv_font_montserrat_20
inline constexpr int kSetupTitleHeight =
    safe_text_box_height(26, kSetupMediumFontLineHeight);
inline constexpr int kSetupLineHeight =
    safe_text_box_height(18, kSetupSmallFontLineHeight);
// The page password is the one string on this page a person transcribes by
// hand, so it renders larger (medium font) than the other text rows and
// wraps (LV_LABEL_LONG_WRAP in render_setup.cpp) instead of clipping - the
// passphrase alphabet (wifi_config::kPassphraseAlphabet) can be wide enough
// in the worst case that a fixed single line cannot be proven to always fit.
// Two medium-font lines is a generous, not exact, upper bound - the same
// "generously sized" approach kSetupStatusHeight below already uses.
inline constexpr int kSetupPasswordLineHeight =
    safe_text_box_height(2 * kSetupMediumFontLineHeight,
                         kSetupMediumFontLineHeight);
// The SSID line is on the critical path now: the QR encodes portal_url, not
// a Wi-Fi join payload, so the panel is the only place this network name
// exists and it must never clip. Its format is fixed
// (wifi_config::setup_ap_ssid: "RLCD-" + 6 uppercase hex digits), so rather
// than a general text-metrics table this proves just the one bound that
// matters: literal prefix width (measured from lv_font_montserrat_14.c) plus
// 6 digits at the widest hex glyph ('D') can never exceed the row's width -
// see setup_ssid_row_width_ok below.
inline constexpr int kSetupSsidPrefixWidthPx = 85;   // "WIFI: RLCD-" at font14
inline constexpr int kSetupHexDigitMaxWidthPx = 12;  // widest of 0-9A-F ('D')
inline constexpr int kSetupSsidHexDigits = 6;
inline constexpr int kSetupSsidWorstCaseWidthPx =
    kSetupSsidPrefixWidthPx + kSetupSsidHexDigits * kSetupHexDigitMaxWidthPx;
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
inline constexpr int kSetupStatusHeight = 3 * kSetupLineHeight;
// Two wrapped lines. One line was never enough: at 168px of usable column the
// English instruction measured 481px and the Chinese 309px, so most of the
// sentence explaining how to use the page had been clipped away since it was
// written. Nothing caught it because the Setup page only renders behind a KEY
// long press, and the overflow log only fires for labels actually drawn.
// Three, because the English sentence needs 352px at 168px per line and
// shaving it to two would mean writing worse English to fit a box. The column
// has the room: setup_layout_fits below is what proves it still does.
inline constexpr int kSetupInstructionsHeight = 3 * kSetupLineHeight;
inline constexpr char kSetupTitle[] = "Setup";
inline constexpr char kSetupNoSsidLabel[] = "AP SSID unavailable";
inline constexpr char kSetupNoPortalPasswordLabel[] = "PAGE PW: unavailable";
inline constexpr char kSetupInstructions[] =
    "Join the open Wi-Fi, then scan QR or open URL and enter PAGE PW";
inline constexpr char kSetupDefaultStatus[] = "Not yet connected";
// A QR failure never blocks setup mode: the AP is open and both portal_url
// and the page password are already on screen, so this fallback is a fully
// usable manual path, not just an apology.
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
                      text_width, kSetupPasswordLineHeight};
  const Rect portal{bounds.x, password.bottom() + kSetupTightLineGap,
                    text_width, kSetupLineHeight};
  const Rect instructions{bounds.x, portal.bottom() + kSetupBlockGap,
                          text_width, kSetupInstructionsHeight};
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

// A forecast column is 49px wide. "Thunderstorm" measures 106px there and
// arrives as "Thun...", which distinguishes it from nothing. The icon narrows
// the answer to one of four silhouettes; this word is what separates showers
// from a thunderstorm inside that, so dropping it loses real information and
// truncating it loses the same information more slowly.
//
// A vocabulary sized for the column instead. Every term below fits at font 14,
// and the mapping is by substring for the same reason
// weather_icon_kind_for_condition is: a WMO wording this does not know about
// should fall through to something honest rather than to a blank.
inline const char* forecast_condition_short(const std::string& condition) {
  if (condition.find("Tstorm") != std::string::npos ||
      condition.find("Thunder") != std::string::npos) return "STRM";
  if (condition.find("Snow") != std::string::npos) return "SNOW";
  if (condition.find("Shower") != std::string::npos) return "SHWR";
  if (condition.find("Icy") != std::string::npos) return "ICE";
  if (condition.find("Drizzle") != std::string::npos) return "DRIZ";
  if (condition.find("Rain") != std::string::npos) return "RAIN";
  if (condition.find("Fog") != std::string::npos) return "FOG";
  if (condition.find("Overcast") != std::string::npos) return "DULL";
  if (condition.find("Partly") != std::string::npos) return "PART";
  if (condition.find("Clear") != std::string::npos ||
      condition.find("Sunny") != std::string::npos) return "CLEAR";
  if (condition.find("Cloud") != std::string::npos) return "CLOUD";
  return "";
}

// "CLOSE  14 Aug" - the session the figures are from. Empty when the source
// did not date them, so the caller draws nothing rather than a placeholder
// date, which would be the one thing worse than no date at all.
// "14 Aug", or empty when the source did not date its figures.
inline std::string market_as_of_short(const app_core::MarketData& market) {
  if (market.as_of_month == 0 || market.as_of_day == 0 ||
      market.as_of_month > 12) {
    return {};
  }
  static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr",
                                            "May", "Jun", "Jul", "Aug",
                                            "Sep", "Oct", "Nov", "Dec"};
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%u %s",
                static_cast<unsigned>(market.as_of_day),
                kMonths[market.as_of_month - 1]);
  return buffer;
}

inline std::string market_as_of_text(const app_core::MarketData& market) {
  if (market.as_of_month == 0 || market.as_of_day == 0) return {};
  static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr",
                                            "May", "Jun", "Jul", "Aug",
                                            "Sep", "Oct", "Nov", "Dec"};
  if (market.as_of_month > 12) return {};
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%s  %u %s",
                text(Text::MarketClose),
                static_cast<unsigned>(market.as_of_day),
                kMonths[market.as_of_month - 1]);
  return buffer;
}

// One per SettingsItem. Derived rather than written as a literal: it was a
// literal 5, and adding a sixth item would have written a row past the end of
// the array in a constexpr function - caught here only because the row count
// happened to be checked. Tie it to the enum and adding an item cannot do that
// again.
inline constexpr int kSettingsRowCount =
    static_cast<int>(SettingsItem::Count);

struct SettingsLayout {
  Rect title;
  // Fixed-size rather than computed per item so the rows cannot shift as
  // values change length underneath them.
  Rect rows[kSettingsRowCount];
  Rect value_column;
  // Full width, below the list. The update check reports things like "Up to
  // date (latest is v0.1.2)", which has no chance in the value column and was
  // arriving on the panel silently chopped.
  Rect status;
};

inline constexpr int kSettingsRowHeight =
    safe_text_box_height(24, kSetupMediumFontLineHeight);
inline constexpr int kSettingsRowGap = 4;
inline constexpr int kSettingsTitleGap = 8;
// The right-hand column carries each row's current value - the version string,
// the language name. Wide enough for "Traditional Chinese" at font 14 and for
// a semantic version, whichever is longer.
inline constexpr int kSettingsValueWidth = 150;
inline constexpr int kSettingsCursorWidth = 14;

// Title, then one row per item, each split into a label on the left and its
// current value on the right. The cursor occupies a fixed gutter so the labels
// do not shift sideways as it moves down.
constexpr SettingsLayout settings_layout(const Rect bounds) {
  const Rect title{bounds.x, bounds.y, bounds.width, kSetupTitleHeight};
  SettingsLayout layout{};
  layout.title = title;
  int y = title.bottom() + kSettingsTitleGap;
  for (int i = 0; i < kSettingsRowCount; ++i) {
    layout.rows[i] = Rect{bounds.x, y, bounds.width, kSettingsRowHeight};
    y += kSettingsRowHeight + kSettingsRowGap;
  }
  layout.value_column =
      Rect{bounds.right() - kSettingsValueWidth, 0, kSettingsValueWidth,
           kSettingsRowHeight};
  layout.status = Rect{bounds.x, y + kSettingsRowGap, bounds.width,
                       kSettingsRowHeight};
  return layout;
}

constexpr bool settings_layout_fits(const Rect content) {
  const SettingsLayout layout = settings_layout(content);
  if (!rect_within(content, layout.title)) return false;
  for (int i = 0; i < kSettingsRowCount; ++i) {
    if (!rect_within(content, layout.rows[i])) return false;
  }
  if (!rect_within(content, layout.status)) return false;
  // The label side must keep usable width once the cursor gutter and the value
  // column are taken out of the row.
  return content.width - kSettingsCursorWidth - kSettingsValueWidth > 80;
}

// Same literal-line-height discipline as the Setup rows above: this header is
// compiled LVGL-free for host tests, so the values come from
// lv_font_montserrat_{28,48}.c rather than font->line_height.
inline constexpr int kOtaPhaseFontLineHeight = 30;    // lv_font_montserrat_28
inline constexpr int kOtaPercentFontLineHeight = 52;  // lv_font_montserrat_48
inline constexpr int kOtaPhaseHeight =
    safe_text_box_height(kOtaPhaseFontLineHeight + 4, kOtaPhaseFontLineHeight);
inline constexpr int kOtaPercentHeight =
    safe_text_box_height(kOtaPercentFontLineHeight + 4,
                         kOtaPercentFontLineHeight);
inline constexpr int kOtaLineHeight =
    safe_text_box_height(18, kSetupSmallFontLineHeight);
inline constexpr int kOtaBlockGap = 10;

// The only instruction that matters while flash is being written, and the
// reason this page takes the screen at all.
inline constexpr char kOtaWarning[] = "DO NOT POWER OFF";
// Shown instead of a percentage when the feeder never learned the total size
// (no Content-Length), rather than inventing a progress figure.
inline constexpr char kOtaProgressUnknown[] = "WORKING";

// One centred column: phase, then the large percentage, then the power
// warning, then a detail line. Pure arithmetic on bounds so host tests can
// drive it in the absolute safe_canvas() frame. Ota has no tray
// (page_shows_tray), so this gets the full canvas height to centre in.
constexpr OtaLayout ota_layout(const Rect bounds) {
  const int block = kOtaPhaseHeight + kOtaBlockGap + kOtaPercentHeight +
                    kOtaBlockGap + kOtaLineHeight + kOtaBlockGap +
                    kOtaLineHeight;
  const int top = bounds.y + (bounds.height - block) / 2;
  const Rect phase{bounds.x, top, bounds.width, kOtaPhaseHeight};
  const Rect percent{bounds.x, phase.bottom() + kOtaBlockGap, bounds.width,
                     kOtaPercentHeight};
  const Rect warning{bounds.x, percent.bottom() + kOtaBlockGap, bounds.width,
                     kOtaLineHeight};
  const Rect detail{bounds.x, warning.bottom() + kOtaBlockGap, bounds.width,
                    kOtaLineHeight};
  return {phase, percent, warning, detail};
}

constexpr bool ota_layout_fits(const Rect content) {
  const OtaLayout layout = ota_layout(content);
  return rect_within(content, layout.phase) &&
         rect_within(content, layout.percent) &&
         rect_within(content, layout.warning) &&
         rect_within(content, layout.detail);
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

// The SSID row's worst-case rendered width (see kSetupSsidWorstCaseWidthPx)
// never exceeds the row's actual width, so setup_ssid_text can never clip
// regardless of which 6 hex digits the device's MAC happens to produce.
constexpr bool setup_ssid_row_width_ok(const Rect content) {
  return kSetupSsidWorstCaseWidthPx <= setup_layout(content).ssid.width;
}

// Formats the SSID/passphrase/portal text block and the tray's live fields.
// Small, pure string helpers kept alongside the layout constants they pair
// with so renderers and the label-only repaint path (ui_app.cpp) share one
// source of truth instead of formatting the same text twice.
inline std::string setup_status_text(const std::string& status) {
  return status.empty() ? std::string(text(Text::SetupDefaultStatus)) : status;
}

// The AP is always open now (no Wi-Fi password), so this line's job is
// naming the network, not printing a passphrase that no longer exists.
// "open" is stated once, on the instructions line, instead of appended
// here as " (OPEN)" - that suffix is exactly what setup_ssid_row_width_ok
// proves would risk clipping the one thing on this row that cannot clip:
// the SSID itself.
inline std::string setup_ssid_text(const std::string& ap_ssid) {
  if (ap_ssid.empty()) return text(Text::SetupNoSsid);
  return std::string(text(Text::SetupWifiPrefix)) + ap_ssid;
}

// portal_password gates the setup page itself, not the network - labelled
// distinctly from the SSID line above so it is not mistaken for a Wi-Fi
// passphrase.
inline std::string setup_password_text(const std::string& portal_password) {
  if (portal_password.empty()) return text(Text::SetupNoPortalPassword);
  return std::string(text(Text::SetupPagePwPrefix)) + portal_password;
}

struct SystemTrayLayout {
  Rect time;
  Rect network;
  Rect battery;
};

// Both indicators live at the right end, leaving the middle empty. They were
// words - "WIFI", "NO WIFI", "BAT 91%" - which is a lot of the tray spent on
// two facts that a shape carries at a glance. The exact charge figure moved to
// the settings page, where it is the number a calibration is compared against.
// Three stacked arcs plus a dot need height; the battery does not, so they
// are sized separately rather than sharing one number that suits neither.
inline constexpr int kTrayWifiIconWidth = 22;
inline constexpr int kTrayWifiIconHeight = 20;
inline constexpr int kTrayIconHeight = 14;
inline constexpr int kTrayBatteryIconWidth = 30;
inline constexpr int kTrayIconGap = 8;

// Sized to match the previous mast band (28) plus its trailing gap (8) so
// the reduced content area on data pages is pixel-identical to before -
// only the addition of an explicit separator changes that 8 into 1 + 7.
inline constexpr int kSystemTrayHeight = 28;
inline constexpr int kSystemTrayContentGap = 7;
inline constexpr int kSystemTrayReservedHeight =
    kSystemTrayHeight + kSeparatorWidth + kSystemTrayContentGap;
inline constexpr int kSystemTrayTimeWidth = 60;
// "Sun, 16 Aug 2026" measures 168px at font 20 - the widest date the clock
// formatter produces, since every weekday and month abbreviation is three
// characters and the day is zero-padded.
inline constexpr int kSystemTrayDateWidth = 176;
inline constexpr int kSystemTrayTimeHeight = 25;
inline constexpr int kSystemTrayCellY = 3;
inline constexpr int kSystemTrayCellHeight = 18;
inline constexpr int kSystemTrayCellGap = 4;
inline constexpr int kSystemTrayBatteryWidth = 64;

// Time flush left, battery flush right, network status filling the middle.
// The page dots briefly lived here too; they are now centred along the bottom
// of the page (see page_dots_geometry), which gives the network cell back the
// width it was sharing. Pure arithmetic on bounds.x/y/width/height, so - like
// setup_layout - it works for both the zero-offset local frame render_page
// builds pages with and the absolute safe_canvas() frame the static_asserts
// below use.
// `wide_leading` gives the first cell room for a date ("Sun, 16 Aug 2026",
// 168px) instead of a clock ("12:34", 58px). Home uses it, because its hero
// already shows the time and the tray carries the date instead.
constexpr SystemTrayLayout system_tray_layout(const Rect bounds,
                                              bool wide_leading = false) {
  const int leading_width =
      wide_leading ? kSystemTrayDateWidth : kSystemTrayTimeWidth;
  const Rect time{bounds.x, bounds.y, leading_width, kSystemTrayTimeHeight};
  // Icon cells now, both flush right: battery last, wifi immediately left of
  // it, and whatever is between them and the leading cell stays empty.
  const int icon_y = bounds.y + (kSystemTrayHeight - kTrayIconHeight) / 2;
  const Rect battery{bounds.right() - kTrayBatteryIconWidth, icon_y,
                     kTrayBatteryIconWidth, kTrayIconHeight};
  const Rect network{battery.x - kTrayIconGap - kTrayWifiIconWidth,
                     bounds.y + (kSystemTrayHeight - kTrayWifiIconHeight) / 2,
                     kTrayWifiIconWidth, kTrayWifiIconHeight};
  return {time, network, battery};
}

// One of the three fixed tray network strings.
inline std::string tray_network_text(const app_core::SetupData& setup) {
  return setup.active ? text(Text::TraySetup)
                      : (setup.connected ? text(Text::TrayWifi)
                                        : text(Text::TrayNoWifi));
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
// Home carries the tray like every other page, so the time, network state and
// battery stay in one fixed place rather than moving as the carousel turns.
// Ota is the sole exception: while it owns the screen the firmware is writing
// its own flash, and a tray would imply a running device to interact with.
constexpr bool page_shows_tray(app_core::PageId page) {
  return page != app_core::PageId::Ota;
}

// Which pages carry the bottom page-dot indicator. Setup and Ota are reachable
// but not part of the rotation, so a position-in-cycle marker would be
// meaningless on them.
constexpr bool page_shows_dots(app_core::PageId page) {
  return page != app_core::PageId::Setup &&
         page != app_core::PageId::Settings && page != app_core::PageId::Ota;
}

// Height reserved along the bottom for the page dots, matching the gap the
// tray leaves below itself so content is inset by the same amount top and
// bottom.
//
// This was briefly forced down to 3px, because the weather forecast column
// stacked to 160px inside a band of (170 - this). Shrinking the oversized
// forecast icon gave that back with room to spare;
// forecast_columns_layout_all_fit below is what proves it still fits, and it
// is load-bearing rather than decorative.
inline constexpr int kPageDotsReservedHeight =
    kPageDotSize + kSystemTrayContentGap;

// Every page derives its content area through this single path, so nothing
// draws into the tray band or under the dots by accident.
constexpr Rect content_bounds(const Rect bounds, app_core::PageId page) {
  int top = bounds.y;
  int height = bounds.height;
  if (page_shows_tray(page)) {
    top += kSystemTrayReservedHeight;
    height -= kSystemTrayReservedHeight;
  }
  if (page_shows_dots(page)) height -= kPageDotsReservedHeight;
  return {bounds.x, top, bounds.width, height};
}

// The strip the dots occupy, below every page's content area. Anchored to the
// bottom of the full page bounds rather than to the content area, so it stays
// put whatever the page above it does.
constexpr Rect page_dots_band(const Rect bounds) {
  return {bounds.x, bounds.bottom() - kPageDotSize, bounds.width,
          kPageDotSize};
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

static_assert(page_shows_tray(app_core::PageId::Home) &&
                  page_shows_tray(app_core::PageId::TaiwanMarket) &&
                  page_shows_tray(app_core::PageId::UsMarket) &&
                  page_shows_tray(app_core::PageId::Weather) &&
                  page_shows_tray(app_core::PageId::Indoor) &&
                  page_shows_tray(app_core::PageId::Setup),
              "every page except the OTA takeover carries the tray, so the "
              "clock, network state and battery never move");
static_assert(!page_shows_tray(app_core::PageId::Ota),
              "the OTA takeover page owns the screen outright");
static_assert(!page_shows_dots(app_core::PageId::Setup) &&
                  !page_shows_dots(app_core::PageId::Ota),
              "pages outside the rotation show no position-in-cycle marker");

static_assert(ota_layout_fits(content_bounds(safe_canvas(),
                                             app_core::PageId::Ota)),
              "OTA layout does not fit its content bounds");

static_assert(content_bounds(safe_canvas(), app_core::PageId::Home).y ==
                      content_bounds(safe_canvas(),
                                     app_core::PageId::Weather).y &&
                  content_bounds(safe_canvas(), app_core::PageId::Home)
                          .height == content_bounds(safe_canvas(),
                                                    app_core::PageId::Weather)
                                         .height,
              "home is laid out on the same content area as every other "
              "carousel page, so the clock cannot drift relative to them");
static_assert(
    content_bounds(safe_canvas(), app_core::PageId::Weather).y ==
        safe_canvas().y + kSystemTrayReservedHeight,
    "a tray page loses exactly the reserved tray height off the top");
static_assert(
    content_bounds(safe_canvas(), app_core::PageId::Weather).bottom() +
            kPageDotsReservedHeight ==
        safe_canvas().bottom(),
    "a dotted page stops short of the bottom by exactly the dot band");
static_assert(
    page_dots_band(safe_canvas()).y >=
        content_bounds(safe_canvas(), app_core::PageId::Weather).bottom(),
    "the dot band sits below the content area rather than over it");
static_assert(
    within_safe_canvas(page_dots_band(safe_canvas())),
    "the dot band stays inside the safe canvas");
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
static_assert(
    setup_ssid_row_width_ok(
        content_bounds(safe_canvas(), app_core::PageId::Setup)),
    "the SSID row can hold the widest possible \"WIFI: RLCD-XXXXXX\" text "
    "without clipping - the QR encodes portal_url, not a Wi-Fi join "
    "payload, so this is the only place the network name is readable");

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
  // Centred, so the cluster stays put as the carousel's page count changes
  // rather than growing leftward from a fixed right edge.
  return {page_count, active_index,
          bounds.x + (bounds.width - total_width) / 2, total_width,
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

// Truthful per-source label - never claims "DEMO" for a real reading.
// "PCF85063" is a real hardware RTC chip read; "SNTP" (net_time's sync
// source, see components/net_time/net_time.cpp) is real network time; only
// "RTC fallback" - the compile-time build-stamp clock used when no RTC chip
// is present or its data looks implausible - is genuinely fake. Any other
// input still gets an honest "UNKNOWN" rather than silently widening the old
// DEMO net around it.
// Empty when the clock is network-synced, which is the expected state and
// needs no annotation. "SOURCE  SYNC" was developer vocabulary on a
// user-facing screen: it told someone reading the time that a subsystem had
// worked, which is not news, and cost a whole row of Home to say it.
//
// The cases worth a word are the ones where the displayed time may be wrong.
inline Text clock_warning_text(const std::string& source) {
  if (source == "SNTP") return Text::Count;  // nothing to say
  if (source == "PCF85063") return Text::ClockFromRtc;
  return Text::ClockNotSynced;
}

// Retained for the tray and for logs, where naming the source is still the
// useful thing to say.
inline std::string compact_clock_source(const std::string& source) {
  if (source == "SNTP") return "SYNC";
  if (source == "PCF85063") return "RTC";
  if (source == "RTC fallback") return "FALLBACK";
  return "UNKNOWN";
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

// The chart region of a market page's primary column, below the
// label/value/change row and its divider. axis_font_line_height is passed in
// (rather than read from the font object, unavailable in this LVGL-free
// header) so render_market.cpp and the static_asserts below share one
// formula - render_market.cpp calls this with small_font()->line_height, the
// static_asserts with the literal kSetupSmallFontLineHeight that constant is
// documented to equal.
constexpr Rect market_chart_rect(const Rect primary,
                                 const int axis_font_line_height) {
  const int axis_height = safe_text_box_height(17, axis_font_line_height);
  return {primary.x + 8, primary.y + 70, primary.width - 16,
          std::max(1, primary.bottom() - primary.y - 71 - axis_height)};
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

// The seven-day forecast row on the weather page, below the current-
// conditions header and divider. Pure arithmetic on content's own
// x/y/width/height, the same convention weather_forecast_rect callers rely
// on, so this works for both the zero-offset frame render_weather.cpp builds
// with and the absolute safe_canvas() frame the static_asserts below use.
inline constexpr int kForecastTopOffset = 82;
inline constexpr int kForecastSideInset = 8;

constexpr Rect weather_forecast_rect(const Rect content) {
  return {content.x + kForecastSideInset, content.y + kForecastTopOffset,
          content.width - 2 * kForecastSideInset,
          std::max(1, content.bottom() - content.y - kForecastTopOffset)};
}

struct ForecastColumnLayout {
  Rect day;
  Rect icon;
  Rect condition;
  Rect high;
  Rect low;
  Rect rain;
};

// High and low used to sit side by side on one line ("H68 L54") - cramped at
// the ~55px column width seven columns leaves across the 388px safe canvas.
// Stacked instead, high above low, each getting the column's full width.
// Every text row height comes from safe_text_box_height (font14's line
// height, see kSetupSmallFontLineHeight) so a font metrics change cannot
// silently grow a row past what forecast_column_layout_fits below proves -
// the same runtime-growth defect fixed by commit b52ff70.
inline constexpr int kForecastRowGap = kSetupTightLineGap;
inline constexpr int kForecastRowHeight =
    safe_text_box_height(18, kSetupSmallFontLineHeight);
// Slightly wider than tall, which is the shape a cloud and a sun actually
// are. An earlier pass grew this to 40x60 chasing legibility and got a worse
// icon rather than a bigger one: at 3:2 portrait the cloud stretched into a
// tower and the sun's disc had to shrink to leave room for its rays. The
// silhouettes were redrawn instead (see draw_cloud in ui_theme.cpp), which is
// what the legibility problem actually needed.
inline constexpr int kForecastIconWidth = 38;
inline constexpr int kForecastIconHeight = 30;

constexpr ForecastColumnLayout forecast_column_layout(const Rect column) {
  const Rect day{column.x, column.y, column.width, kForecastRowHeight};
  const Rect icon{column.x + (column.width - kForecastIconWidth) / 2,
                  day.bottom() + kForecastRowGap, kForecastIconWidth,
                  kForecastIconHeight};
  const Rect condition{column.x, icon.bottom() + kForecastRowGap,
                       column.width, kForecastRowHeight};
  const Rect high{column.x, condition.bottom() + kForecastRowGap,
                  column.width, kForecastRowHeight};
  const Rect low{column.x, high.bottom() + kForecastRowGap, column.width,
                 kForecastRowHeight};
  const Rect rain{column.x, low.bottom() + kForecastRowGap, column.width,
                  kForecastRowHeight};
  return {day, icon, condition, high, low, rain};
}

// Every row of a forecast column fits entirely inside that column.
constexpr bool forecast_column_layout_fits(const Rect column) {
  const ForecastColumnLayout layout = forecast_column_layout(column);
  return rect_within(column, layout.day) && rect_within(column, layout.icon) &&
        rect_within(column, layout.condition) &&
        rect_within(column, layout.high) && rect_within(column, layout.low) &&
        rect_within(column, layout.rain);
}

// No two rows within a forecast column overlap.
constexpr bool forecast_column_layout_disjoint(const Rect column) {
  const ForecastColumnLayout layout = forecast_column_layout(column);
  const std::array<Rect, 6> rects{layout.day,  layout.icon, layout.condition,
                                  layout.high, layout.low,  layout.rain};
  for (std::size_t i = 0; i < rects.size(); ++i) {
    for (std::size_t j = i + 1; j < rects.size(); ++j) {
      if (rects_intersect(rects[i], rects[j])) return false;
    }
  }
  return true;
}

// Every column of the real seven-day forecast row fits and is internally
// disjoint - checked across all 7 columns, not just one, since the last
// column's width differs from the rest (see forecast_columns above).
constexpr bool forecast_columns_layout_all_fit(const Rect forecast) {
  for (const Rect& column : forecast_columns(forecast)) {
    if (!forecast_column_layout_fits(column)) return false;
  }
  return true;
}
constexpr bool forecast_columns_layout_all_disjoint(const Rect forecast) {
  for (const Rect& column : forecast_columns(forecast)) {
    if (!forecast_column_layout_disjoint(column)) return false;
  }
  return true;
}

static_assert(
    forecast_columns_layout_all_fit(
        weather_forecast_rect(content_bounds(safe_canvas(),
                                             app_core::PageId::Weather))),
    "every forecast column's stacked day/icon/condition/high/low/rain rows "
    "fit entirely inside their column");
static_assert(
    forecast_columns_layout_all_disjoint(
        weather_forecast_rect(content_bounds(safe_canvas(),
                                             app_core::PageId::Weather))),
    "no two rows within a forecast column overlap");
// Page dots have moved into the system tray (see SystemTrayLayout below), so
// the forecast column no longer has anything to stay clear of at its own
// bottom edge - forecast_rows_clear_page_dots and its static_assert were
// removed along with that overlap concern.

// As normalize_chart_samples, but over the first `count` samples only, so a
// partly-filled series spans the full width instead of running into its own
// empty tail. `count` below 2 returns nothing usable - the caller should not
// be drawing a line from fewer than two points.
constexpr std::array<ChartPoint, 8> normalize_chart_samples_n(
    const std::array<int, 8>& samples, const Rect bounds, std::size_t count) {
  std::array<ChartPoint, 8> points{};
  if (count < 2) return points;
  if (count > samples.size()) count = samples.size();
  int minimum = samples[0];
  int maximum = samples[0];
  for (std::size_t i = 1; i < count; ++i) {
    if (samples[i] < minimum) minimum = samples[i];
    if (samples[i] > maximum) maximum = samples[i];
  }
  const int range = maximum - minimum;
  for (std::size_t index = 0; index < count; ++index) {
    const int x =
        bounds.x + static_cast<int>((index * static_cast<std::size_t>(
                                         bounds.width - 1)) /
                                    (count - 1));
    int y = bounds.y + bounds.height / 2;
    if (range != 0) {
      y = bounds.y + ((maximum - samples[index]) * (bounds.height - 1)) / range;
    }
    if (y < bounds.y) y = bounds.y;
    if (y >= bounds.bottom()) y = bounds.bottom() - 1;
    points[index] = {x, y};
  }
  return points;
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

// A page whose backing struct is not yet MarketData/WeatherData/IndoorData
// ::valid keeps its slot in the carousel and its title, but every number -
// primary value, chart, forecast column - is replaced by this single
// placeholder rather than a zero, an empty percentage, or a chart drawn from
// an all-zero sample array. One shared string/geometry instead of four
// copies, one per renderer (Taiwan market, US market, weather, indoor).
inline constexpr char kNoDataLabel[] = "NO DATA";
// Appended to a real (not fabricated) reading whose WeatherData::stale flag
// is set - an old reading is still real data and should be shown, marked,
// not dropped to the placeholder above.
inline constexpr char kStaleSuffix[] = " OLD";

// Reserves room for the page's own title row (e.g. "TAIWAN MARKET",
// "INDOOR") above the placeholder - generously sized the same way
// kSetupStatusHeight is above, not an exact font-metric fit, just enough
// that the placeholder box built below can never land under the title text.
inline constexpr int kNoDataTitleReserve =
    safe_text_box_height(26, kSetupSmallFontLineHeight);
inline constexpr int kNoDataBoxHeight = safe_text_box_height(
    kSetupMediumFontLineHeight, kSetupMediumFontLineHeight);

// Centers a NO DATA box within `area`, below the reserved title band. `area`
// is a page's primary content rect (market_layout(content).primary for the
// market/indoor pages) or the whole content rect for a page with no side
// column (weather) - pure arithmetic on x/y/width/height, so it works for
// both the zero-offset frame renderers receive and the absolute
// safe_canvas() frame the static_asserts below use, the same convention
// setup_layout already follows.
constexpr Rect no_data_rect(const Rect area) {
  const int top = area.y + kNoDataTitleReserve;
  const int remaining = area.bottom() - top;
  const int gap =
      remaining > kNoDataBoxHeight ? (remaining - kNoDataBoxHeight) / 2 : 0;
  return {area.x, top + gap, area.width, kNoDataBoxHeight};
}

// TaiwanMarket and UsMarket share the same tray-reduced content geometry
// (content_bounds only branches on page_shows_tray, true for both), so one
// proof against TaiwanMarket covers both market pages.
static_assert(
    rect_within(
        content_bounds(safe_canvas(), app_core::PageId::TaiwanMarket),
        no_data_rect(market_layout(content_bounds(
                                       safe_canvas(),
                                       app_core::PageId::TaiwanMarket))
                        .primary)),
    "the market no-data placeholder stays inside the tray-reduced content "
    "bounds");
static_assert(
    no_data_rect(market_layout(content_bounds(
                                    safe_canvas(),
                                    app_core::PageId::TaiwanMarket))
                     .primary)
            .y >= market_layout(content_bounds(
                                    safe_canvas(),
                                    app_core::PageId::TaiwanMarket))
                          .primary.y +
                      kNoDataTitleReserve,
    "the market no-data placeholder never overlaps the reserved title row");
static_assert(
    rect_within(content_bounds(safe_canvas(), app_core::PageId::Indoor),
                no_data_rect(market_layout(content_bounds(
                                               safe_canvas(),
                                               app_core::PageId::Indoor))
                                .primary)),
    "the indoor no-data placeholder stays inside the tray-reduced content "
    "bounds");
static_assert(
    rect_within(
        content_bounds(safe_canvas(), app_core::PageId::Weather),
        no_data_rect(
            content_bounds(safe_canvas(), app_core::PageId::Weather))),
    "the weather no-data placeholder - no side column, so it centers in the "
    "whole content rect - stays inside the tray-reduced content bounds");

// market.valid can be true with has_intraday false (a daily-close-only
// source, e.g. TWSE): the label/value/change figures are real and still
// render, but there is no intraday series to plot, so the chart, dotted
// grid, and time-axis labels are skipped rather than drawing a flat,
// invented line. This is a short, honest line in the space the chart would
// have occupied instead - distinct from kNoDataLabel above, which means the
// whole page has nothing to show.
inline constexpr char kNoIntradayLabel[] = "NO INTRADAY DATA";

// Centers a short placeholder line within the chart region `chart` would
// otherwise occupy - no title reserve (unlike no_data_rect above), since the
// chart region already sits below the market page's own label/value/change
// row, not directly under a page title.
constexpr Rect chart_placeholder_rect(const Rect chart) {
  const int gap =
      chart.height > kNoDataBoxHeight ? (chart.height - kNoDataBoxHeight) / 2 : 0;
  return {chart.x, chart.y + gap, chart.width, kNoDataBoxHeight};
}

// TaiwanMarket and UsMarket share the same tray-reduced content geometry, so
// one proof against TaiwanMarket covers both.
static_assert(
    rect_within(
        market_layout(
            content_bounds(safe_canvas(), app_core::PageId::TaiwanMarket))
            .primary,
        chart_placeholder_rect(market_chart_rect(
            market_layout(content_bounds(safe_canvas(),
                                         app_core::PageId::TaiwanMarket))
                .primary,
            kSetupSmallFontLineHeight))),
    "the no-intraday chart placeholder stays inside the market page's "
    "primary column");
static_assert(
    rect_within(
        market_chart_rect(
            market_layout(content_bounds(safe_canvas(),
                                         app_core::PageId::TaiwanMarket))
                .primary,
            kSetupSmallFontLineHeight),
        chart_placeholder_rect(market_chart_rect(
            market_layout(content_bounds(safe_canvas(),
                                         app_core::PageId::TaiwanMarket))
                .primary,
            kSetupSmallFontLineHeight))),
    "the no-intraday chart placeholder stays inside the chart region it "
    "replaces, not stretched to fill the whole primary column");

// ---------------------------------------------------------------------------
// Home page: Clock Hero plus one situational tile.
//
// "首頁現在擺的東西有點太多，看不到重點" - Home dropped from three stacked
// right-hand tiles to one, chosen by priority below, and the left column's
// TODAY/NEXT/WEATHER-ALERT rows are gone entirely rather than duplicating
// whatever the one tile already says. The vertical space that freed up goes
// to spacing the clock/date/sync block out instead of leaving it cramped at
// the top - see home_layout below.
enum class HomeTileKind { Battery, Weather, Market, Indoor, None };

// Below this, a low reading is worth surfacing on Home even without an
// over-voltage condition - deliberately generous so the warning appears
// well before a shutdown, not right at the edge of one.
inline constexpr uint8_t kHomeLowBatteryPercent = 20;

constexpr bool home_battery_notable(const app_core::BatteryData& battery) {
  return battery.valid && (battery.overvoltage_warning ||
                           battery.percent <= kHomeLowBatteryPercent);
}

// Priority, highest first: a battery problem (over-voltage or low charge)
// outranks a weather alert, which outranks a plain informative reading -
// weather (even without an alert), then market, then indoor, then a plain
// battery reading. Each tier is skipped when its backing data is not valid
// rather than shown as NO DATA in the one slot meant to carry the important
// thing; None only when nothing on the snapshot is valid at all.
constexpr HomeTileKind choose_home_tile(const app_core::AppSnapshot& snapshot) {
  if (home_battery_notable(snapshot.battery)) return HomeTileKind::Battery;
  if (snapshot.weather.valid && snapshot.weather.alert) return HomeTileKind::Weather;
  if (snapshot.weather.valid) return HomeTileKind::Weather;
  if (snapshot.taiwan_market.valid) return HomeTileKind::Market;
  if (snapshot.indoor.valid) return HomeTileKind::Indoor;
  if (snapshot.battery.valid) return HomeTileKind::Battery;
  return HomeTileKind::None;
}

// The next-best tile after `first`, or None when nothing else has real data.
//
// Home shows at most two. One left roughly two thirds of a 264px column empty
// and no amount of enlarging three short rows fills that; three was the
// clutter the focus pass removed. Two is the count that uses the space without
// going back to a list. The second only ever appears when it has something
// measured to say, so an idle board still shows one tile rather than a
// placeholder invented to fill the gap.
constexpr HomeTileKind choose_home_second_tile(
    const app_core::AppSnapshot& snapshot, HomeTileKind first) {
  if (first != HomeTileKind::Weather && snapshot.weather.valid) {
    return HomeTileKind::Weather;
  }
  if (first != HomeTileKind::Indoor && snapshot.indoor.valid) {
    return HomeTileKind::Indoor;
  }
  if (first != HomeTileKind::Market && snapshot.taiwan_market.valid) {
    return HomeTileKind::Market;
  }
  if (first != HomeTileKind::Battery && snapshot.battery.valid) {
    return HomeTileKind::Battery;
  }
  return HomeTileKind::None;
}

// Splits a side column into `count` evenly-sized cells. A count of one keeps
// the whole column, so a page with a single thing to say does not show an
// empty half - the count is always how many tiles have real data, never a
// fixed grid with blanks in it.
inline constexpr int kStackedTileGap = 8;
constexpr Rect stacked_tile_cell(const Rect column, int index, int count) {
  if (count <= 1) return column;
  const int total_gap = kStackedTileGap * (count - 1);
  const int height = (column.height - total_gap) / count;
  return {column.x, column.y + index * (height + kStackedTileGap),
          column.width, height};
}

// Kept as the Home-specific name the tests and renderer already use.
constexpr Rect home_tile_cell(const Rect column, int index, int count) {
  return stacked_tile_cell(column, index, count);
}

// Highest and lowest of the intraday series. Only meaningful when the provider
// actually supplied one - a daily-close source repeats its close, and a range
// of zero drawn as a range would read as "the market did not move".
struct MarketRange {
  int high = 0;
  int low = 0;
};
constexpr MarketRange market_intraday_range(
    const std::array<int, 8>& samples) {
  MarketRange range{samples[0], samples[0]};
  for (std::size_t i = 1; i < samples.size(); ++i) {
    if (samples[i] > range.high) range.high = samples[i];
    if (samples[i] < range.low) range.low = samples[i];
  }
  return range;
}

struct HomeLayout {
  Rect hero;
  Rect date;
  Rect sync;
  Rect tile;
};

inline constexpr int kHomeRightWidth = 118;
inline constexpr int kHomeSplitGap = 12;
inline constexpr int kHomeBottomMargin = 12;
inline constexpr int kHomeHeroFontLineHeight = 52;  // lv_font_montserrat_48
inline constexpr int kHomeRowFontLineHeight =
    kSetupMediumFontLineHeight;  // 22, lv_font_montserrat_20
// lv_font_montserrat is no longer the hero face; rlcd_digits_128 is, and its
// line height is 92. Kept as a literal for the same reason every other font
// metric here is: this header compiles without LVGL for host tests.
inline constexpr int kHomeHeroLineHeight = 92;
inline constexpr int kHomeHeroHeight =
    safe_text_box_height(kHomeHeroLineHeight + 4, kHomeHeroLineHeight);
inline constexpr int kHomeRowHeight =
    safe_text_box_height(26, kHomeRowFontLineHeight);
inline constexpr int kHomeBlockGap = 10;

// Clock Hero, date and sync rows stacked and vertically centered in the left
// column - the height freed by dropping Home's TODAY/NEXT/WEATHER-ALERT rows
// (see choose_home_tile above) becomes breathing room here instead of a dead
// gap, and date/sync move up to the medium font (20px, was 14px) now that
// there is room. `bounds` is Home's own content area (page_shows_tray(Home)
// is false, so this is the untouched safe canvas at render time); pure
// arithmetic on bounds.x/y/width/height, so it works for both that
// zero-offset-in-practice frame and the absolute safe_canvas() frame the
// static_asserts below use, the same convention setup_layout follows.
// Clock across the full width, tiles side by side beneath it.
//
// The clock used to share the row with a narrow tile column, which capped how
// wide it could be - and a horizontal HH:MM is width-limited, not
// height-limited, so capping the width left the whole lower half of a 264px
// column empty no matter how large the font went. Given the full 388px the
// clock roughly triples in area, and the tiles get cells that are wider than
// they are tall instead of the reverse.
//
// `sync` is the clock-source warning. Its row is reserved even though it
// usually draws nothing: overlaying it on the gap would have it paint across a
// tile on exactly the boots where something is already wrong, and an empty
// band under the clock is a better use of 28px than a collision.
constexpr HomeLayout home_layout(const Rect bounds) {
  const Rect hero{bounds.x + 2, bounds.y, bounds.width - 4, kHomeHeroHeight};
  const Rect sync{bounds.x + 3, hero.bottom(), bounds.width - 6,
                  kHomeRowHeight};
  const int tiles_y = sync.bottom() + kHomeBlockGap;
  const Rect tile{bounds.x, tiles_y, bounds.width,
                  bounds.bottom() - tiles_y};
  // `date` is unused now that the tray carries it; kept in the struct so the
  // shape does not churn, collapsed to nothing so a stray draw is visible.
  return {hero, Rect{bounds.x, bounds.y, 0, 0}, sync, tile};
}

// Splits the tile row left/right. Same contract as stacked_tile_cell: one tile
// takes the whole row rather than leaving a blank half.
constexpr Rect home_tile_column(const Rect row, int index, int count) {
  if (count <= 1) return row;
  const int width = (row.width - kStackedTileGap) / 2;
  return {row.x + index * (width + kStackedTileGap), row.y, width, row.height};
}

// Every rect home_layout produces fits entirely inside `content`.
constexpr bool home_layout_fits(const Rect content) {
  const HomeLayout layout = home_layout(content);
  return rect_within(content, layout.hero) && rect_within(content, layout.date) &&
        rect_within(content, layout.sync) && rect_within(content, layout.tile);
}

// No two rects in the layout overlap.
constexpr bool home_layout_disjoint(const Rect content) {
  const HomeLayout layout = home_layout(content);
  const std::array<Rect, 4> rects{layout.hero, layout.date, layout.sync,
                                  layout.tile};
  for (std::size_t i = 0; i < rects.size(); ++i) {
    for (std::size_t j = i + 1; j < rects.size(); ++j) {
      if (rects_intersect(rects[i], rects[j])) return false;
    }
  }
  return true;
}

static_assert(
    home_layout_fits(content_bounds(safe_canvas(), app_core::PageId::Home)),
    "every home rect fits entirely inside home's content bounds");
static_assert(
    home_layout_disjoint(content_bounds(safe_canvas(), app_core::PageId::Home)),
    "no two home rects overlap");
static_assert(
    home_layout(content_bounds(safe_canvas(), app_core::PageId::Home))
            .hero.height >= kHomeHeroFontLineHeight,
    "the clock hero box is at least as tall as the hero font's line height");

}  // namespace ui
