#pragma once

#include <cstddef>
#include <cstdint>

#include "ui_strings.hpp"

namespace ui {

// Ordered by how often a row is actually acted on, not by topic: this menu
// is walked with two physical buttons, so every row above the one you want
// costs a press whether or not that row does anything - a dead row first is
// a tax on every single visit. Actionable rows lead, in roughly descending
// order of use; display-only rows (Runtime, Battery, Firmware) trail, in
// roughly ascending order of "why anyone would look" - Runtime is read
// often, Battery only during calibration, and Firmware's row does nothing
// at all when selected (see its own comment below), so it sinks furthest.
enum class SettingsItem : uint8_t {
  // Cycles through Language, applied immediately so the effect is visible on
  // the very row that changed it. First: the most-used, most-obviously-live
  // row in the menu.
  Language,
  WifiSetup,
  CheckUpdates,
  // Projected time left, from the persisted discharge history rather than
  // from the current reading. Display only, like Battery and Firmware below -
  // but the one of the three someone actually comes here to read.
  Runtime,
  // Raw millivolts beside the percentage. Display only, and here rather than
  // on a data page because it exists for one job: comparing the board's
  // reading against a multimeter so CONFIG_BATTERY_CALIBRATION_PERMILLE can be
  // set. Until that is done the percentage and the overvoltage thresholds are
  // both untrustworthy, and digging the figure out of a serial log is enough
  // friction that the calibration does not happen. A diagnostic row, read far
  // less often than Runtime, which is why it sits below it.
  Battery,
  // Shows the running version. Selecting it does nothing - it is here because
  // "check for updates" is meaningless if you cannot see what you have. Last,
  // deliberately: nothing happens when you select it, so of every row in the
  // menu this is the one a press should least often have to walk past.
  Firmware,
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
  // Install what the last check found. Same downloader, same ota::Session,
  // same progress and rollback path as a push from a machine on the network -
  // the only difference is which side started it.
  StartUpdateInstall,
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
  // wherever it was left days ago. The offer is cleared too: an update found
  // days ago should be re-checked rather than installed on trust.
  void reset() {
    focused_ = SettingsItem::Language;
    update_offered_ = false;
  }

  // Set when a check finds something installable, so the same row switches
  // from asking to offering.
  void set_update_offered(bool offered) { update_offered_ = offered; }
  bool update_offered() const { return update_offered_; }

 private:
  SettingsItem focused_ = SettingsItem::Language;
  bool update_offered_ = false;
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
