#include "battery.hpp"

#ifdef ESP_PLATFORM

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>

namespace board {
namespace {

constexpr char kTag[] = "battery";
constexpr adc_unit_t kUnit = ADC_UNIT_1;
constexpr adc_channel_t kChannel = ADC_CHANNEL_3;  // GPIO4, see board_pins.hpp
constexpr adc_atten_t kAtten = ADC_ATTEN_DB_12;     // full range for the divided cell voltage
constexpr adc_bitwidth_t kBitwidth = ADC_BITWIDTH_12;
constexpr int kSampleCount = 8;  // averaged to damp ADC noise, not a filter

adc_oneshot_unit_handle_t g_unit = nullptr;
adc_cali_handle_t g_cali = nullptr;
bool g_cali_available = false;
bool g_initialized = false;

// Fallback when the efuse curve-fitting calibration scheme is not burned:
// scales the 12-bit raw reading against the 12 dB attenuation's ~3.3 V
// nominal full scale.
int raw_to_millivolts_fallback(int raw) {
  constexpr int kFullScaleMillivolts = 3300;
  constexpr int kFullScaleCounts = 4095;
  return raw * kFullScaleMillivolts / kFullScaleCounts;
}

bool ensure_init() {
  if (g_initialized) return true;

  adc_oneshot_unit_init_cfg_t unit_cfg{};
  unit_cfg.unit_id = kUnit;
  if (adc_oneshot_new_unit(&unit_cfg, &g_unit) != ESP_OK) return false;

  adc_oneshot_chan_cfg_t chan_cfg{};
  chan_cfg.atten = kAtten;
  chan_cfg.bitwidth = kBitwidth;
  if (adc_oneshot_config_channel(g_unit, kChannel, &chan_cfg) != ESP_OK) {
    return false;
  }

  adc_cali_curve_fitting_config_t cali_cfg{};
  cali_cfg.unit_id = kUnit;
  cali_cfg.chan = kChannel;
  cali_cfg.atten = kAtten;
  cali_cfg.bitwidth = kBitwidth;
  g_cali_available =
      adc_cali_create_scheme_curve_fitting(&cali_cfg, &g_cali) == ESP_OK;
  if (!g_cali_available) {
    ESP_LOGW(kTag, "curve-fitting calibration unavailable; using raw-scaling fallback");
  }

  g_initialized = true;
  return true;
}

}  // namespace

bool battery_read(app_core::BatteryData& out) {
  out = app_core::BatteryData{};
  if (!ensure_init()) return false;

  int sum_mv = 0;
  for (int sample = 0; sample < kSampleCount; ++sample) {
    int raw = 0;
    if (adc_oneshot_read(g_unit, kChannel, &raw) != ESP_OK) return false;
    int mv = 0;
    if (!g_cali_available || adc_cali_raw_to_voltage(g_cali, raw, &mv) != ESP_OK) {
      mv = raw_to_millivolts_fallback(raw);
    }
    sum_mv += mv;
  }
  const int adc_mv = sum_mv / kSampleCount;
  const int cell_mv = app_core::battery_millivolts(adc_mv);
  if (!app_core::battery_reading_valid(cell_mv)) return true;  // valid stays false

  out.valid = true;
  out.millivolts = cell_mv;
  out.percent = app_core::battery_percent(cell_mv);
  return true;
}

}  // namespace board

#endif  // ESP_PLATFORM
