#include "ui_app.hpp"

#include <esp_app_desc.h>

namespace ui {
namespace {

// The value shown to the right of a row's label. Empty for rows that are an
// action rather than a setting - "check for updates" has no current value, and
// inventing one would make it look like a reading.
std::string row_value(SettingsItem item, const std::string& status) {
  switch (item) {
    case SettingsItem::Firmware: {
      const esp_app_desc_t* desc = esp_app_get_description();
      return desc != nullptr ? desc->version : "unknown";
    }
    case SettingsItem::Language:
      return language_name(ui::language());
    case SettingsItem::CheckUpdates:
      // Carries the result of the last check once there is one, so the row
      // that started the work is also the row that reports it.
      return status;
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
  (void)snapshot;
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

    const std::string value = row_value(item, status);
    if (!value.empty()) {
      const Rect value_rect{row.right() - kSettingsValueWidth, row.y,
                            kSettingsValueWidth, row.height};
      lv_obj_t* value_obj = label(parent, value.c_str(), value_rect,
                                  font_small(), LV_TEXT_ALIGN_RIGHT);
      if (is_focused) apply_setup_status_style(value_obj, true);
    }
  }
}

}  // namespace ui
