#pragma once

#include <cstddef>
#include <cstdint>

#include "ui_strings.hpp"

namespace ui {

enum class SettingsItem : uint8_t {
  // Shows the running version. Selecting it does nothing - it is here because
  // "check for updates" is meaningless if you cannot see what you have.
  Firmware,
  // Cycles through Language, applied immediately so the effect is visible on
  // the very row that changed it.
  Language,
  CheckUpdates,
  WifiSetup,
  Count,
};

// Which button does what right now. The device has two usable buttons and
// three contexts, so their meaning has to change - and because it changes, the
// screen has to say what it currently is. Every context here has a matching
// hint pair rendered along the bottom of the page.
enum class InputContext : uint8_t {
  // KEY previous page, BOOT next page, KEY-long Wi-Fi setup, BOOT-long menu.
  Carousel,
  // KEY next item, BOOT select, KEY-long back out.
  Menu,
  // Nothing: flash is being written and no input may interrupt it.
  Locked,
};

// What selecting an item asked for. The menu itself performs nothing; it
// reports, and the caller - which owns the network, NVS and the LVGL lock -
// decides. Keeps this whole file pure and host-testable.
enum class SettingsAction : uint8_t {
  None,
  LanguageChanged,
  StartUpdateCheck,
  EnterWifiSetup,
  Exit,
};

class SettingsMenu {
 public:
  SettingsItem focused() const { return focused_; }
  std::size_t focused_index() const {
    return static_cast<std::size_t>(focused_);
  }
  static constexpr std::size_t count() {
    return static_cast<std::size_t>(SettingsItem::Count);
  }

  // Wraps at the end rather than stopping. With one button to move and no way
  // to go back up, a cursor that stops at the bottom is a cursor you cannot
  // return from.
  void focus_next();

  // Applies the item's own behaviour where that behaviour is purely local
  // (language cycling) and reports everything else for the caller to carry
  // out.
  SettingsAction activate();

  // Reset on every entry, so the menu always opens on the first row instead of
  // wherever it was left days ago.
  void reset() { focused_ = SettingsItem::Firmware; }

 private:
  SettingsItem focused_ = SettingsItem::Firmware;
};

// The label shown for an item, in the active language.
Text settings_item_label(SettingsItem item);

// Which two hints the bottom band shows for a context. Returned as Text so the
// hints translate with everything else.
struct InputHints {
  Text key;
  Text boot;
  // False for Locked, where the correct thing to say is nothing at all rather
  // than naming buttons that are being ignored.
  bool visible;
};
InputHints input_hints(InputContext context);

}  // namespace ui
