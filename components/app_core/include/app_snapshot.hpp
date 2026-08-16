#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace app_core {

// Setup is not scheduled by PageRegistry/the carousel; it is shown only via
// the ui::publish_snapshot() seam while snapshot.setup.active is true.
// Setup and Ota are addressable pages that never enter the carousel: each is
// shown because its own state says so, not because rotation reached it.
enum class PageId { Home, TaiwanMarket, UsMarket, Weather, Indoor, Setup, Settings, Ota };
enum class DemoScenario { MorningAlert, TaiwanSession, NightSession };

struct ClockData {
  std::string hero;
  std::string date;
  std::string source;
};

// Register order is PCF85063 seconds, minutes, hours, day, weekday, month,
// year. This is deliberately a value-only type so host tests can validate the
// read path without pulling in an I2C driver.
struct RtcDateTime {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
};

uint8_t days_in_month(uint16_t year, uint8_t month);
RtcDateTime advance_rtc_datetime(RtcDateTime start, uint64_t elapsed_seconds);
bool decode_pcf85063(const uint8_t* registers, std::size_t length,
                     RtcDateTime& decoded);

// Providers set valid only once real data has landed. A false flag means the
// page still renders and keeps its slot in the carousel, but shows a NO DATA
// placeholder instead of numbers - never a fabricated value.
struct MarketData {
  bool valid = false;
  std::string display_name;
  std::string primary_label;
  int primary_value = 0;
  double primary_change_percent = 0.0;
  std::string secondary_label;
  int secondary_value = 0;
  double secondary_change_percent = 0.0;
  // A daily-close-only source has no intraday series. Repeating the close
  // across the array would draw a flat line, which reads as "the market did
  // not move" rather than "there is no intraday data" - real numbers, invented
  // shape. False means the UI must not draw a chart at all.
  bool has_intraday = false;
  std::array<int, 8> intraday_samples{};
  // The session these figures are from, as the source reported it. Zero means
  // the source did not say.
  //
  // A closed market is the normal weekend state, and a page showing Thursday's
  // close with no date invites reading it as today's. Both providers supply
  // this - TWSE as a ROC-calendar date field, Yahoo as regularMarketTime - so
  // it is reported rather than inferred from the device clock, which would be
  // a guess about a market on the other side of the world.
  uint16_t as_of_year = 0;
  uint8_t as_of_month = 0;
  uint8_t as_of_day = 0;
};

struct WeatherCurrent {
  std::string location;
  std::string condition;
  double temperature_c = 0.0;
  uint8_t rain_probability_percent = 0;
};

struct WeatherDay {
  std::string day;
  std::string condition;
  double high_c = 0.0;
  double low_c = 0.0;
  uint8_t rain_probability_percent = 0;
};

struct WeatherData {
  bool valid = false;
  // True when the last successful fetch is old enough that the reading should
  // be shown as stale rather than current.
  bool stale = false;
  WeatherCurrent current;
  std::array<WeatherDay, 7> seven_day{};
  bool alert = false;
};

struct IndoorData {
  bool valid = false;
  double temperature_c = 0.0;
  uint8_t humidity_percent = 0;
  // Deterministic mock history for this snapshot-only slice.
  std::array<double, 8> temperature_history_c{};
  // How many of the history slots hold a real reading, oldest first. Zero
  // until the first interval elapses. Without it the array's leading zeros are
  // indistinguishable from measurements of 0 C, and the chart drew a line
  // through them - a shape made of numbers nobody recorded.
  uint8_t temperature_history_count = 0;
};

struct Availability {
  bool taiwan_market = true;
  bool us_market = true;
  bool weather = true;
  bool indoor = true;
};

// Published by the provisioning task and consumed on the LVGL thread. Strings
// are pre-rendered so the UI never reaches into wifi_config or esp_wifi.
struct SetupData {
  bool active = false;
  bool connected = false;
  std::string ap_ssid;
  // Regenerated on every entry into setup mode and never persisted, so the
  // screen is the only place it exists. This is not a Wi-Fi credential - the
  // setup AP is open - it gates the setup web portal itself.
  std::string portal_password;
  std::string portal_url;
  std::string qr_payload;
  std::string status;
  // True only for a genuine failure status (wrong password, network not
  // found, generic connect failure) so the UI can render it more
  // prominently than neutral progress without sniffing the status string.
  bool error = false;
};

// Sampled from the GPIO4 / ADC1 channel 3 divider. valid stays false until a
// plausible reading lands so the tray degrades to a blank cell instead of 0%.
//
// overvoltage_warning is detection only, never prevention: this board's
// battery connection is a read-only sense divider with no charger-enable
// GPIO exposed to the app (see the pin table in
// .agents/skills/esp32-s3-rlcd-dev/references/official-development.md), so
// firmware cannot stop or limit charging. Overcharge protection is the
// charge IC's and the cell's own protection board's job; this field just
// lets the UI flag a reading that looks wrong.
struct BatteryData {
  bool valid = false;
  int millivolts = 0;
  uint8_t percent = 0;
  bool overvoltage_warning = false;
};

// Applies the board's 3x sense divider and a calibration_permille trim
// (raw * 3 * calibration_permille / 1000) to a raw ADC-reported millivolts
// value, returning the estimated cell millivolts. Exposed with an explicit
// permille argument so host tests can exercise both scaling directions;
// battery_millivolts() below is the production entry point that always uses
// the Kconfig-configured value.
int battery_millivolts_scaled(int adc_millivolts, int calibration_permille);

// Applies CONFIG_BATTERY_CALIBRATION_PERMILLE (default 1000, i.e. no trim).
int battery_millivolts(int adc_millivolts);

// Below this, there is no battery installed or the divider path is open;
// callers should leave BatteryData::valid false rather than report a
// misleadingly plausible-looking 0%.
inline constexpr int kBatteryValidThresholdMillivolts = 2500;
bool battery_reading_valid(int millivolts);

// Maps a single-cell Li-ion millivolts reading to 0..100% using a piecewise
// discharge-curve table (a naive linear 3000-4200 mV map is badly wrong
// mid-curve). Clamped to 0..100 at both ends.
uint8_t battery_percent(int millivolts);

// Overvoltage thresholds for a single-cell Li-ion pack, above a normal
// 4200 mV CC/CV termination plus typical charger/ADC tolerance. These are
// meaningless before CONFIG_BATTERY_CALIBRATION_PERMILLE has been tuned per
// the calibration note on battery_millivolts() above: at the untuned
// default of 1000, the reported millivolts can be several percent off the
// true cell voltage, so calibration is a prerequisite for this warning to
// mean anything, not an optional refinement.
inline constexpr int kBatteryOvervoltageWarningMillivolts = 4250;
inline constexpr int kBatteryOvervoltageDangerMillivolts = 4300;

// True at or above kBatteryOvervoltageWarningMillivolts (and therefore also
// true in the danger range below). False for a genuinely full 4200 mV cell.
bool battery_overvoltage_warning(int millivolts);
// True at or above kBatteryOvervoltageDangerMillivolts.
bool battery_overvoltage_danger(int millivolts);

// What the firmware is doing to itself, put on the panel because every one of
// these states is one the user must not act on blindly: Receiving and Writing
// are the windows where cutting power leaves a half-written slot, and
// RolledBack is the only evidence a boot ever failed once the board is back up
// and looking normal.
//
// The compiled Montserrat font is ASCII-only, so ota_phase_label() below
// returns English. Rendering these in Chinese needs a font subset built for
// it, which is a separate change from making the states visible at all.
enum class OtaPhase : uint8_t {
  Idle,
  // A push arrived over the network and is waiting for someone at the board to
  // accept it. Nothing has been written and nothing has been erased; the
  // request is held open until an answer or a timeout.
  AwaitingConfirm,
  // Bytes arriving from a feeder (browser upload or URL pull) and going
  // straight into the inactive slot; percent is meaningful only here.
  Receiving,
  // Image received and being finalised - hash checked, boot partition set.
  Writing,
  // First boot of a freshly written image. It has not been marked valid yet,
  // so a reset in this window rolls the board back to the previous slot.
  Verifying,
  // The previous image failed its verification window and the bootloader came
  // back to this one. Sticky for the session: this is the only trace the user
  // would otherwise ever see of a failed update.
  RolledBack,
  Failed,
};

// Never fabricates progress: percent stays 0 unless a feeder actually knows
// the total size, and the UI shows the phase alone when it does not.
struct OtaData {
  OtaPhase phase = OtaPhase::Idle;
  uint8_t percent = 0;
  bool percent_known = false;
  // Short ASCII reason, only ever set alongside Failed or RolledBack.
  std::string detail;
};

// True while the update state must own the screen outright. Deliberately
// excludes Verifying: that window is a normal boot the user should not be
// locked out of, and it ends on its own.
constexpr bool ota_owns_screen(const OtaData& ota) {
  return ota.phase == OtaPhase::Receiving || ota.phase == OtaPhase::Writing;
}

// The confirm prompt owns the screen too, but unlike a write in progress it
// wants the buttons - they are the answer. Kept separate so the input layer
// can tell "ignore everything" from "these two keys mean yes and no".
constexpr bool ota_awaits_confirm(const OtaData& ota) {
  return ota.phase == OtaPhase::AwaitingConfirm;
}

// ASCII only - see the OtaPhase comment above.
const char* ota_phase_label(OtaPhase phase);

struct AppSnapshot {
  OtaData ota;
  ClockData clock;
  MarketData taiwan_market;
  MarketData us_market;
  WeatherData weather;
  WeatherData new_york_weather;
  IndoorData indoor;
  Availability availability;
  SetupData setup;
  BatteryData battery;
  DemoScenario scenario = DemoScenario::TaiwanSession;
};

AppSnapshot make_mock_snapshot(DemoScenario scenario);

}  // namespace app_core
