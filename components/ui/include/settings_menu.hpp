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
  // Cycles through VolumePreset the same way Language cycles - immediate
  // effect, audible on the row that just changed it. Right next to Language
  // for the same reason: both are actionable and both apply themselves on
  // the spot, unlike WifiSetup/CheckUpdates below, which start something
  // that takes longer than one row press to finish.
  Volume,
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
  VolumeChanged,
  StartUpdateCheck,
  // Install what the last check found. Same downloader, same ota::Session,
  // same progress and rollback path as a push from a machine on the network -
  // the only difference is which side started it.
  StartUpdateInstall,
  EnterWifiSetup,
  Exit,
};

// Discrete, on-device presets for the volume of *locally generated* sound
// only - alarms and notification tones played through modules/audio. Before
// this existed the only control was `POST /beep?vol=` on a debug-only
// route, which does not exist in a release build; this is the normal,
// on-device replacement.
//
// This must apply ONLY to modules/audio's own tone playback and NEVER to
// AirPlay/streamed playback, when that exists. In AirPlay the source device
// owns the volume - it is sent over RTSP from the phone - and multiplying
// that by a second, local scale here would mean turning the volume up on
// the phone stops fully taking effect: a genuinely maddening bug to live
// with, not a corner case. When streaming is wired up, its own playback
// path must call the codec at the source's requested level directly and
// must not read VolumePreset, volume_preset(), or route through
// set_volume_preset_store_handler/the settings row at all. Nothing in this
// type assumes otherwise, and nothing about adding AirPlay later should
// touch it.
enum class VolumePreset : uint8_t { Off, Low, Medium, High, Count };

// 0-100 codec percentage for each preset. Medium is 50 on purpose: it is
// the only level anyone has actually listened to on real hardware (see
// modules/audio/audio.cpp's kOutputVolumePercent, which shares this exact
// number), so it is kept as a known-good anchor rather than the midpoint of
// a guessed scale. Low/High are an even +/-25 spread around it, not
// independently validated. High stops at 75, not 100: a hardware sweep
// already found 80% "a bit loud" (see modules/audio/README.md's "The
// volume default"), so this stays clear of that.
constexpr int volume_preset_percent(VolumePreset preset) {
  switch (preset) {
    case VolumePreset::Off:
      return 0;
    case VolumePreset::Low:
      return 25;
    case VolumePreset::Medium:
      return 50;
    case VolumePreset::High:
      return 75;
    case VolumePreset::Count:
      break;
  }
  return 50;
}

VolumePreset volume_preset();

// Only notifies the store handler below on an actual change, same as
// set_language - cycling back to the preset already in force writes
// nothing. Does not touch hardware or play anything by itself; see
// set_volume_changed_handler in ui_app.hpp for where the audible,
// interactive side of a settings-row change actually happens (registered
// and invoked from there, not from here, specifically so the boot-time
// restore below - which also calls this - never plays a startup beep).
void set_volume_preset(VolumePreset value);

// Registers where a change is written so it survives a reboot - same
// pattern as set_language_store_handler, for the same reason: this
// translation unit is compiled by the host tests, which have no flash.
void set_volume_preset_store_handler(void (*handler)(VolumePreset value));

// The row's value-column text, in the active language - "Off"/"Low"/
// "Medium"/"High", unlike language_name() these do translate with the rest
// of the UI, since they are not naming themselves the way a language does.
const char* volume_preset_name(VolumePreset value);

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
