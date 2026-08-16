#include "ui_app.hpp"

#include <esp_app_desc.h>

#include <cstdio>

namespace ui {
namespace {

// The value shown to the right of a row's label. Empty for rows that are an
// action rather than a setting - "check for updates" has no current value, and
// inventing one would make it look like a reading.
std::string row_value(SettingsItem item,
                      const app_core::BatteryData& battery) {
  switch (item) {
    case SettingsItem::Language:
      return language_name(ui::language());
    case SettingsItem::CheckUpdates:
      // No value of its own: the result of a check goes in the full-width row
      // below the list, because it is a sentence rather than a figure.
      //
      // This case was left empty when that moved, and an empty case falls
      // through - so the update row started showing the battery's millivolts.
      // The explicit return is what stops it.
      return {};
    case SettingsItem::Runtime: {
      // Every branch that is not a measured projection says so rather than
      // printing a number. A runtime figure is the kind of thing that gets
      // believed and planned around, so the only case that produces one is
      // the one where the discharge was actually observed.
      switch (battery.runtime.trend) {
        case app_core::PowerTrend::Charging:
          return text(Text::StatusCharging);
        case app_core::PowerTrend::Unknown:
          return text(Text::StatusCollecting);
        case app_core::PowerTrend::Steady:
          return "--";
        case app_core::PowerTrend::Discharging:
          break;
      }
      if (!battery.runtime.known) return "--";
      const unsigned minutes = battery.runtime.minutes_remaining;
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
      std::snprintf(buffer, sizeof(buffer), "%d mV  %u%%", battery.millivolts,
                    static_cast<unsigned>(battery.percent));
      return buffer;
    }
    case SettingsItem::Firmware: {
      const esp_app_desc_t* desc = esp_app_get_description();
      return desc != nullptr ? desc->version : "unknown";
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

    const std::string value = row_value(item, snapshot.battery);
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
