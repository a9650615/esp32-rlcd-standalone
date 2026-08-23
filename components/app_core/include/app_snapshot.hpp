#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "history.hpp"

namespace app_core {

// Setup is not scheduled by PageRegistry/the carousel; it is shown only via
// the ui::publish_snapshot() seam while snapshot.setup.active is true.
// Setup and Ota are addressable pages that never enter the carousel: each is
// shown because its own state says so, not because rotation reached it.
enum class PageId { Home, TaiwanMarket, UsMarket, Weather, Indoor, Setup, Settings, Ota, NowPlaying };
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
// Inverse of decode_pcf85063: seven registers starting at VL_SECONDS, ready to
// write straight to the chip.
//
// Writing the seconds register clears the oscillator-stop flag in bit 7, which
// is the whole point. A PCF85063 sets that bit when it has lost its
// timekeeping and never clears it on its own, so a chip that has never been
// written reads as invalid forever - which is what made this board fall back
// to its build timestamp on every boot despite having a working RTC on the
// bus.
//
// Returns false rather than writing nonsense if the date is out of range.
bool encode_pcf85063(const RtcDateTime& clock, uint8_t* registers,
                     std::size_t length);

bool decode_pcf85063(const uint8_t* registers, std::size_t length,
                     RtcDateTime& decoded);

// Providers set valid only once real data has landed. A false flag means the
// page still renders and keeps its slot in the carousel, but shows a NO DATA
// placeholder instead of numbers - never a fabricated value.
// Shared by MarketData::intraday_samples and market_parse.hpp's own
// IndexQuote::samples - one target resolution for both, not two constants
// that could drift apart. ~4 px/point against this project's actual market
// chart width (roughly 260 px) - see modules/market's own notes on the
// tradeoff. Large enough that Taiwan's full 09:00-13:30 session at 5-minute
// bars (54 of them) fits with no reduction at all; the US session at the
// same granularity (78 bars over 6.5 hours) still needs a mild one.
inline constexpr std::size_t kIntradaySampleCount = 64;

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
  std::array<int, kIntradaySampleCount> intraday_samples{};
  // How many of the leading intraday_samples slots hold a real, distinct
  // point - same reason IndoorData::temperature_history_count exists: early
  // in a session there may be fewer real bars than the array's own target
  // resolution, and the unused trailing slots must not be read as zero-
  // valued data. render_market.cpp reads only the first
  // intraday_sample_count entries (via normalize_chart_samples_n) and
  // market_intraday_range's own count parameter, never the full array
  // width, when has_intraday is true. Meaningless (left at its 0 default)
  // whenever has_intraday is false.
  uint8_t intraday_sample_count = 0;
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
  // Fraction (0.0-1.0) of the session intraday_samples actually covers, for
  // scaling the chart's x-axis (render_market.cpp) so a session still in
  // progress does not stretch its real samples across the same width a
  // completed session would - every pixel of a full-width chart otherwise
  // implies a finished trading day, which mirrors why has_intraday exists:
  // a plausible-looking shape must not claim data that is not there.
  //
  // Computed by market_parse.cpp's parse_yahoo_quote() directly from the
  // response's own meta.currentTradingPeriod.regular.start/end and the
  // last timestamp in the response's own series - both epoch seconds, no
  // device clock, no timezone, no DST arithmetic, and (verified live
  // against the real endpoint before this was built) present in an
  // ordinary chart response. This is why Taiwan and US both get a genuine
  // value from the same mechanism, unlike an earlier version of this field
  // that derived Taiwan's alone from market_schedule.hpp's hardcoded
  // 09:00-13:30 constants and the board's own RTC - superseded, not kept
  // alongside this.
  //
  // Default 1.0 ("complete") on purpose: a source with no notion of this
  // (the TWSE fallback, which only ever carries a completed close) must
  // render exactly as it always has, at full width.
  float session_elapsed_fraction = 1.0f;
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
  // Smoothed (see smoothed_battery_millivolts() below), not the raw sample -
  // consecutive raw readings moved 4078/4050/4069 mV on real hardware, which
  // alone moved the displayed percent. overvoltage_warning below is computed
  // from the raw reading instead, on purpose: a real overvoltage condition
  // must not wait out a smoothing window before it is flagged.
  int millivolts = 0;
  uint8_t percent = 0;
  bool overvoltage_warning = false;
  // True when recent voltage readings suggest external power is present -
  // see voltage_suggests_charging() below. This board has no charge-detect
  // line to read (Waveshare documents the CHG pin as an LED only; the
  // charger IC's own STAT output, confirmed against the schematic, does not
  // reach any ESP32-S3 GPIO either), so this is inferred from the one thing
  // available: the terminal voltage a charger holds up near its CV setpoint
  // regardless of the cell's actual state of charge. Deliberately separate
  // from AppSnapshot::battery_runtime's PowerTrend::Charging (a slower,
  // slope-based signal from a different task's history) rather than folded
  // into it - see that field's own comment on why the two must not merge.
  // A caller wanting the fuller picture reads both; this field alone
  // answers "should the percentage be trusted right now".
  bool charging = false;
  // No RuntimeEstimate here on purpose - see AppSnapshot::battery_runtime
  // below for where it lives and why. This struct is republished wholesale
  // every ~30 s by the battery sampler; a field belonging to a different
  // task on a different cadence must not be able to ride along with that
  // assignment and get silently overwritten with a default-constructed one.
  // charging above is fine here specifically because it is produced by that
  // same sampler, on the same cadence - not a different task's field riding
  // along.
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

// Plain moving average over whatever recent calibrated millivolts readings
// the caller kept - not a fuel-gauge model, just enough smoothing that the
// displayed value stops visibly jittering when consecutive ADC samples move
// by tens of millivolts on their own (observed on hardware: 4078, 4050,
// 4069 mV within a couple of minutes - each one a legitimate reading, but
// showing every one of them makes the percentage read as "wrong" even
// though the underlying voltage barely moved). `recent_millivolts` may be in
// any order - only the values are used - and `count` must be > 0.
int smoothed_battery_millivolts(const int* recent_millivolts, int count);

// Fast charging signal from voltage alone: evidence within the couple of
// minutes it takes to collect `count` readings, not the hour
// PowerTrend::Charging (history.hpp) needs to see a slope. A cell under any
// real load does not sit at or above kChargingVoltageThresholdMillivolts
// (~4.15 V - comfortably below a single-cell charger's ~4.20 V CV
// regulation point, and above where a real load pulls a full cell down
// within a few minutes of being unplugged), so every one of the caller's
// last `count` raw readings landing up there - not just one - is treated as
// evidence external power is present.
//
// Honest false positive, stated rather than hidden: a genuinely full cell,
// unplugged moments ago, reads identically to one still charging for as
// long as it takes a real load to pull it back down - typically a couple
// of minutes, which is also roughly this function's own reaction time.
// There is no hardware signal on this board to tell the two cases apart -
// Waveshare documents the CHG pin as an LED only, and the charger IC's own
// STAT output (confirmed against the schematic) does not reach any
// ESP32-S3 GPIO either.
//
// Takes raw (unsmoothed) readings deliberately, not
// smoothed_battery_millivolts()'s output: smoothing lags a real voltage
// step by close to its own window, which would slow down the one signal
// this function exists to make fast.
inline constexpr int kChargingVoltageThresholdMillivolts = 4150;
bool voltage_suggests_charging(const int* recent_millivolts, int count);

// The other half of the charging signal, and the half that was missing.
//
// voltage_suggests_charging() above cannot tell a charger at its CV setpoint
// from a full cell that was unplugged, because at one instant they read the
// same. Its own comment predicted the resulting false positive would clear
// "typically a couple of minutes". Measured on this board, 69 samples over
// 34.5 minutes with the cable out: 4214 mV -> 4196 mV, a slope of
// -0.66 mV/min = -40 mV/hour. From a full 4210 mV that is an hour before the
// reading crosses back under 4150 mV - not a couple of minutes, and long
// enough that the panel shows a charging icon for an hour after unplugging.
//
// Direction is the discriminator. A charger holds the terminal voltage at CV
// (slope ~0, or positive on a cell that is not yet full); nothing makes an
// unplugged cell gain voltage. So charging is "high AND not falling", and
// this is the "not falling" part.
//
// It is necessarily slow, and the full window is the minimum rather than a
// nicety. Single readings carry about +/-10 mV of ADC noise against a
// 0.66 mV/min signal, so a short window is movement smaller than its own
// noise floor - a host test over the real measured series confirms the fit
// correctly declines to call ten minutes of it.
//
// Sizing this is not "more samples is better". The standard error of a
// least-squares slope goes as sigma * sqrt(dt) / span^1.5, so the *span*
// dominates and the sample interval barely matters:
//
//   40 samples at 30 s = 20 min span -> SE about 16 mV/hour
//  132 samples at  5 s = 11 min span -> SE about 16 mV/hour
//
// Six times the samples buys a span 6^(1/3) = 1.8x shorter, not 6x. The
// window below takes that trade because eleven minutes of a wrong charging
// icon is better than twenty, and 528 bytes is cheap - but it is worth
// writing down that the first estimate of this was "about eight minutes",
// from assuming sample count drove the precision. It does not.
//
// The boundary sits at -20 mV/hour: between a charger's ~0 and a real
// discharge's measured -40, and far enough from zero not to be noise given
// the SE above.
//
// There is no fast version of this on hardware with no charge-detect line.
// A slow signal that is right beats the fast one that was wrong for an hour.
//
// `ordered_millivolts` must be oldest-first with even spacing - unlike
// smoothed_battery_millivolts(), which only uses the values, a slope needs
// the order.
//
// The refusal threshold is a minimum *span*, not a minimum count, for the
// same reason the sizing arithmetic above is about span: forty samples thirty
// seconds apart and a hundred and thirty-two five seconds apart carry the same
// information, and a rule written in samples would accept one and reject the
// other. Returns false below it, because "unknown" must not read as "falling"
// - that would suppress the charging icon for the first eleven minutes after
// every boot spent on a charger.
// 133, not 132. The span of a window is (count - 1) intervals, not count:
// 132 samples 5 s apart cover 655 s, five short of the minimum below, so a
// full window was refused and voltage_is_falling() could never fire on
// hardware at all - which left charging = level_high && !falling reading as
// plain level_high, the exact false positive that whole function was added to
// kill. The sampler asserts the relationship rather than restating the
// arithmetic; see kBatterySlopePeriodMs in app_main.cpp.
inline constexpr int kChargingSlopeWindow = 133;          // sampler's buffer
inline constexpr int kChargingSlopeMinSpanSeconds = 660;  // 11 minutes
inline constexpr float kDischargeSlopeMillivoltsPerHour = -20.0f;
bool voltage_is_falling(const int* ordered_millivolts, int count,
                        int seconds_per_sample);

// The same fit read the other way, and the answer to the question
// voltage_suggests_charging() cannot reach: is a cell that is nowhere near
// full being charged right now.
//
// That function fires only at or above 4150 mV, within about 50 mV of a full
// cell. A cell at half charge sits around 3.85 V on the charger, so for the
// entire hours-long climb from empty to nearly full - the whole span where
// someone actually wants to know a charger is attached - the level rule reads
// exactly like a cell on nothing. Direction is what separates them: nothing
// makes an unplugged cell gain voltage, and a charging one gains it fast.
//
// Sizing, against the same measured series that sized the falling threshold
// (-0.66 mV/min unplugged, +/-10 mV of ADC noise per reading, so a standard
// error near 16 mV/hour over the 11-minute minimum span):
//
//   this board discharging, measured        -40 mV/hour
//   noise alone, 1 sigma                    +/-16 mV/hour
//   kChargingRiseMillivoltsPerHour           +60 mV/hour  <- the bar
//   a cell charging below its CV setpoint   +100 mV/hour and up
//
// The bar sits at nearly four sigma of noise and still well under the
// slowest part of a real charge - the plateau between 3.7 V and 3.9 V, which
// is where a charging cell spends most of its time and moves slowest. It is
// deliberately three times further from zero than the falling threshold:
// falling is a confirmation that only ever suppresses an icon the level rule
// already raised, while this raises one on its own, and the failure it has to
// survive is a big load dropping off (the panel or the radio going idle),
// which lets the terminal voltage rebound and fits as a positive slope.
//
// Same span rule and same refusal below it as voltage_is_falling(): "unknown"
// must not read as "rising" either.
inline constexpr float kChargingRiseMillivoltsPerHour = 60.0f;
bool voltage_is_rising(const int* ordered_millivolts, int count,
                       int seconds_per_sample);

// The least-squares fit both of the above read, in millivolts per hour.
// Exposed because they are two thresholds on one number and a caller (or a
// test) reading that number directly is clearer than inferring it from two
// bools. Returns false, leaving `out` untouched, when the window spans less
// than kChargingSlopeMinSpanSeconds - the same refusal, in the same place.
bool battery_voltage_slope(const int* ordered_millivolts, int count,
                           int seconds_per_sample, float* out);

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
  // Version of the image being offered or written, read out of the image's own
  // descriptor rather than declared by whoever sent it. Empty when unknown.
  //
  // It has to come from the image: this string appears on the screen where
  // someone decides whether to install, and a version supplied by the pusher
  // is a version an attacker on the LAN chooses. The 112-byte prefix carries
  // it, so it costs nothing to take the authoritative one.
  std::string version;

  // Deliberately no `notes` field here. This struct is what a LAN push's
  // confirm prompt renders (ota_confirm.cpp's request_confirm(), driven from
  // components/wifi_provision/portal.cpp, via main/app_main.cpp's
  // show_update_prompt()) - the one path where the text on offer is chosen
  // by whoever is pushing, not by GitHub. A GitHub release's body earns a
  // different amount of trust (see components/ota/include/ota_notes.hpp) and
  // is shown from a different place entirely: the settings row that reports
  // an update check (main/app_main.cpp's update_check_task(), through
  // ui::set_update_status()), which never touches OtaData at all. Adding a
  // notes field here would make it one call site's mistake away from
  // showing a pusher's free text on the one screen where that text has
  // never been trustworthy.
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

// The tray's transient indicators are not a snapshot field: see
// tray_registry.hpp. A module registers its own icon and toggles it
// directly through that registry, rather than through this struct and the
// wifi_provision publish/consume pipeline every other field here goes
// through - there is no per-module state to carry here, and no enum to
// extend when a second module (audio today, AirPlay someday) needs a tray
// icon. This is deliberate: an earlier version of this file had exactly
// such an enum (app_core::TrayActivity) naming "Speaker", and it was
// removed because naming a specific module's concept in core is precisely
// the coupling the module contract exists to prevent.

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
  // Deliberately its own top-level field, not a member of BatteryData above:
  // the battery sampler (every ~30 s) and the history/runtime estimator
  // (every ~5 min) are two different tasks, on two different cadences,
  // each publishing its own struct wholesale through wifi_provision's
  // set_battery()/set_runtime_estimate(). When this lived inside
  // BatteryData, set_battery()'s `snapshot_.battery = battery;` - a whole-
  // struct assignment from a freshly-built, always-default-constructed-
  // runtime BatteryData - silently overwrote whatever the estimator had
  // just published, roughly nine ticks out of every ten. A comment on the
  // setter explaining not to do that was already in place and did not
  // prevent it; disjoint fields do, because there is no longer a shared
  // struct for either writer's wholesale assignment to reach across into.
  RuntimeEstimate battery_runtime;
  DemoScenario scenario = DemoScenario::TaiwanSession;
};

AppSnapshot make_mock_snapshot(DemoScenario scenario);

}  // namespace app_core
