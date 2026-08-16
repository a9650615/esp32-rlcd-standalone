#include "settings_menu.hpp"

namespace ui {

void SettingsMenu::focus_next() {
  focused_ = static_cast<SettingsItem>((focused_index() + 1) % count());
}

SettingsAction SettingsMenu::activate() {
  switch (focused_) {
    case SettingsItem::Firmware:
      // Display only. Selecting the version row does nothing on purpose:
      // there is no sensible action, and inventing one would make the row a
      // trap for someone pressing their way down the list.
      return SettingsAction::None;
    case SettingsItem::Language: {
      const auto next = static_cast<Language>(
          (static_cast<std::size_t>(ui::language()) + 1) %
          static_cast<std::size_t>(Language::Count));
      set_language(next);
      return SettingsAction::LanguageChanged;
    }
    case SettingsItem::CheckUpdates:
      // One row, two jobs: it asks until there is an answer, then it offers.
      // A separate install row would sit there inert most of the time and
      // still need the check to have run first.
      return update_offered_ ? SettingsAction::StartUpdateInstall
                             : SettingsAction::StartUpdateCheck;
    case SettingsItem::WifiSetup:
      return SettingsAction::EnterWifiSetup;
    case SettingsItem::Battery:
      // Display only, like the firmware row.
      return SettingsAction::None;
    case SettingsItem::Count:
      break;
  }
  return SettingsAction::None;
}

Text settings_item_label(SettingsItem item) {
  switch (item) {
    case SettingsItem::Firmware:
      return Text::SettingsFirmware;
    case SettingsItem::Language:
      return Text::SettingsLanguage;
    case SettingsItem::CheckUpdates:
      return Text::SettingsCheckUpdates;
    case SettingsItem::WifiSetup:
      return Text::SettingsWifiSetup;
    case SettingsItem::Battery:
      return Text::SettingsBattery;
    case SettingsItem::Count:
      break;
  }
  return Text::SettingsTitle;
}

InputHints input_hints(InputContext context) {
  switch (context) {
    case InputContext::Carousel:
      return {Text::HintPrevPage, Text::HintNextPage, true};
    case InputContext::Menu:
      return {Text::HintNextItem, Text::HintSelect, true};
    case InputContext::Locked:
      // Naming buttons that are deliberately ignored would be worse than
      // silence: it invites the one interaction that must not happen while
      // flash is being written.
      return {Text::HintNextItem, Text::HintSelect, false};
  }
  return {Text::HintNextItem, Text::HintSelect, false};
}

}  // namespace ui
