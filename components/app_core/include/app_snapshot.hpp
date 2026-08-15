#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace app_core {

// Setup is not scheduled by PageRegistry/the carousel; it is shown only via
// the ui::publish_snapshot() seam while snapshot.setup.active is true.
enum class PageId { Home, TaiwanMarket, UsMarket, Weather, Indoor, Setup };
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

struct AppSnapshot {
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
