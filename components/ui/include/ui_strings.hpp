#pragma once

#include <cstdint>
#include <string>

namespace ui {

enum class Language : uint8_t {
  English,
  TraditionalChinese,
  // Keep last: the count, and what a stored value out of range falls back from.
  Count,
};

// Every piece of fixed interface text. Values that came from a sensor or a
// network fetch are not in here - they are data, not interface, and are shown
// exactly as measured.
enum class Text : uint16_t {
  NoData,
  NoIntradayData,
  StaleSuffix,
  ComfortBand,

  SetupTitle,
  SetupNoSsid,
  SetupNoPortalPassword,
  SetupInstructions,
  SetupDefaultStatus,
  SetupQrUnavailable,

  OtaAwaitingConfirm,
  OtaConfirmHint,
  OtaUpdating,
  OtaFinishing,
  OtaVerifying,
  OtaRolledBack,
  OtaFailed,
  OtaDoNotPowerOff,
  OtaWorking,

  SettingsTitle,
  SettingsFirmware,
  SettingsLanguage,
  SettingsCheckUpdates,
  SettingsWifiSetup,
  SettingsBattery,
  SettingsChecking,

  HintNextItem,
  HintSelect,
  HintBack,
  HintPrevPage,
  HintNextPage,

  TileBattery,
  // "SENSOR", not "INDOOR". The SHTC3 sits on the board beside the
  // ESP32-S3 and reads its own neighbourhood, which self-heating puts above
  // room temperature (see the calibration note in shtc3.hpp). Calling it
  // indoor temperature invites someone to read it as the room's.
  TileIndoor,
  TileMarket,
  TileWeather,
  TileHistory,
  HistoryCollecting,
  TileRange,
  MarketClose,
  TileStatus,
  TitleTaiwanMarket,
  TitleUsMarket,
  TrayWifi,
  TrayNoWifi,
  TraySetup,
  StatusAlert,
  StatusLowBattery,
  StatusOvervoltage,
  StatusDry,
  StatusHumid,
  ChartOpen,
  ChartMid,
  ChartClose,
  SetupWifiPrefix,
  SetupPagePwPrefix,
  SetupConnecting,

  ClockFromRtc,
  ClockNotSynced,

  LanguageEnglish,
  LanguageTraditionalChinese,

  Count,
};

// The active language. Defaults to English: this is an open-source project
// with a wider audience than its author, and the choice is one selection away
// on the settings page and remembered across reboots.
Language language();
void set_language(Language value);

// Never returns nullptr. A Text with no translation yet falls back to the
// English string rather than rendering blank, so an untranslated entry looks
// like an oversight instead of a broken screen.
const char* text(Text id);

// The same lookup for an explicit language, which the settings page needs so
// it can show each language in its own script rather than in the current one.
const char* text_in(Language language, Text id);

// Name of a language as written in that language, for the settings list.
const char* language_name(Language value);

// One spelling of a temperature for the whole firmware.
//
// Four sites had three between them - "28 C", "30.2 C", "30.2" with a degree
// sign and no unit - so the same reading looked like a different kind of
// number depending on which page you were on. The degree sign is available:
// LVGL's Montserrat faces carry U+00B0 in their second cmap range, which is
// why the sensor page could already use it while the others spelled it with a
// space.
//
// `decimals` stays a parameter because it is a real difference: a forecast
// high is not measured to a tenth and printing one would claim precision the
// source does not have.
std::string temperature_text(float celsius, int decimals);

}  // namespace ui
