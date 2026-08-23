#include "ui_app.hpp"

#include <esp_app_desc.h>

#include <cstdio>

namespace ui {
namespace {

// The value shown to the right of a row's label. Empty for rows that are an
// action rather than a setting - "check for updates" has no current value, and
// inventing one would make it look like a reading.
std::string row_value(SettingsItem item, const app_core::BatteryData& battery,
                      const app_core::RuntimeEstimate& runtime) {
  switch (item) {
    case SettingsItem::Language:
      return language_name(ui::language());
    case SettingsItem::Volume:
      return volume_preset_name(ui::volume_preset());
    case SettingsItem::CheckUpdates:
      // No value of its own: the result of a check goes in the full-width row
      // below the list, because it is a sentence rather than a figure.
      //
      // This case was left empty when that moved, and an empty case falls
      // through - so the update row started showing the battery's millivolts.
      // The explicit return is what stops it.
      return {};
    case SettingsItem::Runtime: {
      // The voltage-based signal is asked first, before the trend: it calls a
      // charger within about eleven minutes, where the percent-per-hour trend
      // needs the best part of an hour of five-minute slots to turn around,
      // and a runtime printed in that gap is the number someone plans their
      // afternoon around while the cell is in fact filling.
      if (battery_is_charging(battery, runtime.trend)) {
        return text(Text::StatusCharging);
      }
      // Every branch that is not a measured projection says so rather than
      // printing a number. A runtime figure is the kind of thing that gets
      // believed and planned around, so the only case that produces one is
      // the one where the discharge was actually observed.
      switch (runtime.trend) {
        case app_core::PowerTrend::Charging:
          return text(Text::StatusCharging);
        case app_core::PowerTrend::Unknown:
          return text(Text::StatusCollecting);
        case app_core::PowerTrend::Steady:
          return "--";
        case app_core::PowerTrend::Discharging:
          break;
      }
      if (!runtime.known) return "--";
      const unsigned minutes = runtime.minutes_remaining;
      char buffer[24];
      std::snprintf(buffer, sizeof(buffer), "%uh %02um", minutes / 60,
                    minutes % 60);
      return buffer;
    }
    case SettingsItem::Battery: {
      // Millivolts first: that is the number a multimeter is compared against.
      // The percentage follows so the row is still readable as a battery
      // level, and both are marked absent rather than shown as zero when the
      // divider reads below a plausible cell voltage.
      if (!battery.valid) return "--";
      char buffer[24];
      // While charging, the measured voltage is the charger's output, not
      // the cell's state of charge - a percentage computed from it would be
      // confidently wrong, not merely imprecise (see
      // battery_percent_trustworthy(), ui_data.hpp). Millivolts still shown:
      // it is real regardless of charging state, and is exactly the number
      // a multimeter comparison needs.
      if (!battery_percent_trustworthy(battery, runtime.trend)) {
        std::snprintf(buffer, sizeof(buffer), "%d mV  %s", battery.millivolts,
                      text(Text::StatusCharging));
        return buffer;
      }
      std::snprintf(buffer, sizeof(buffer), "%d mV  %u%%", battery.millivolts,
                    static_cast<unsigned>(battery.percent));
      return buffer;
    }
    case SettingsItem::Firmware: {
      // Version alone cannot tell two builds apart when only the hash
      // changed - which is exactly the ambiguity that made three OTA
      // pushes unverifiable with no cable attached (see portal.cpp's
      // /build route, added for the same reason). The first 8 hex
      // characters of app_elf_sha256 make the running build identifiable
      // by eye, from the panel, on the one channel that still works when
      // the board is not serving what it should: no network needed.
      //
      // "version hash8" measures to ~94px at font_small() (Montserrat 14)
      // for a build like "0.1.2 fe865dcf" - comfortably inside
      // kSettingsValueWidth (150px), checked with the offline glyph-width
      // pass over lv_font_montserrat_14.c's adv_w table (see
      // ui-text-and-layout memory) rather than assumed.
      const esp_app_desc_t* desc = esp_app_get_description();
      if (desc == nullptr) return "unknown";
      char buffer[64];
      std::snprintf(buffer, sizeof(buffer),
                    "%s %02x%02x%02x%02x%02x%02x%02x%02x", desc->version,
                    desc->app_elf_sha256[0], desc->app_elf_sha256[1],
                    desc->app_elf_sha256[2], desc->app_elf_sha256[3],
                    desc->app_elf_sha256[4], desc->app_elf_sha256[5],
                    desc->app_elf_sha256[6], desc->app_elf_sha256[7]);
      return buffer;
    }
    case SettingsItem::WifiSetup:
    case SettingsItem::Count:
      break;
  }
  return {};
}

}  // namespace

// Outside the carousel, like Setup and Ota. Entered by a BOOT long press and
// driven with the same two buttons under different meanings, which is why the
// hint band along the bottom is not decoration: it is the only thing telling
// anyone that KEY has stopped paging and started moving a cursor.
void render_settings(lv_obj_t* parent, const app_core::AppSnapshot& snapshot,
                     Rect bounds, std::size_t page_index,
                     std::size_t page_count, UiContext* context) {
  (void)page_index;
  (void)page_count;
  apply_surface(parent);
  const SettingsLayout layout = settings_layout(bounds);

  label(parent, text(Text::SettingsTitle), layout.title, font_medium());

  const std::size_t focused =
      context != nullptr ? context->settings_focus : 0;
  const std::string status =
      context != nullptr ? context->settings_status : std::string();

  for (std::size_t i = 0; i < SettingsMenu::count(); ++i) {
    const auto item = static_cast<SettingsItem>(i);
    const Rect row = layout.rows[i];

    // A filled bar behind the focused row rather than a marker glyph beside
    // it: on a reflective panel without a backlight, an inverted block is the
    // one cursor that stays obvious across the whole range of ambient light
    // this thing sits in.
    const bool is_focused = i == focused;

    const Rect label_rect{row.x + kSettingsCursorWidth, row.y,
                          row.width - kSettingsCursorWidth -
                              kSettingsValueWidth,
                          row.height};
    lv_obj_t* label_obj = label(parent, text(settings_item_label(item)),
                                label_rect, font_small(), LV_TEXT_ALIGN_LEFT);
    // Inverted block for the focused row, reusing the convention the setup
    // status and the navigation overlay already use. On a reflective panel
    // with no backlight this is the one cursor that stays obvious across the
    // whole range of ambient light the device lives in; a marker glyph beside
    // the text disappears in dim rooms.
    if (is_focused) apply_setup_status_style(label_obj, true);

    const std::string value =
        row_value(item, snapshot.battery, snapshot.battery_runtime);
    if (!value.empty()) {
      const Rect value_rect{row.right() - kSettingsValueWidth, row.y,
                            kSettingsValueWidth, row.height};
      lv_obj_t* value_obj = label(parent, value.c_str(), value_rect,
                                  font_small(), LV_TEXT_ALIGN_RIGHT);
      if (is_focused) apply_setup_status_style(value_obj, true);
    }
  }

  // Its own full-width row, wrapping rather than clipping: this is the one
  // string on the page whose length is not under this code's control.
  if (!status.empty()) {
    label_wrapped(parent, status.c_str(), layout.status, font_small(),
                  LV_TEXT_ALIGN_LEFT);
  }
}

}  // namespace ui
