#include "app_snapshot.hpp"

#include "test_support.hpp"

HOST_TEST(battery_percent_matches_curve_breakpoints_exactly) {
  EXPECT_EQ(app_core::battery_percent(4200), 100);
  EXPECT_EQ(app_core::battery_percent(4060), 90);
  EXPECT_EQ(app_core::battery_percent(3980), 80);
  EXPECT_EQ(app_core::battery_percent(3920), 70);
  EXPECT_EQ(app_core::battery_percent(3870), 60);
  EXPECT_EQ(app_core::battery_percent(3820), 50);
  EXPECT_EQ(app_core::battery_percent(3790), 40);
  EXPECT_EQ(app_core::battery_percent(3700), 30);
  EXPECT_EQ(app_core::battery_percent(3620), 20);
  EXPECT_EQ(app_core::battery_percent(3500), 10);
  EXPECT_EQ(app_core::battery_percent(3300), 5);
  EXPECT_EQ(app_core::battery_percent(3000), 0);
}

HOST_TEST(battery_percent_interpolates_between_breakpoints) {
  // Midpoint of the 3790-3700 segment (40% to 30%, 90 mV span) is 35%.
  EXPECT_EQ(app_core::battery_percent(3745), 35);
  // Midpoint of the 4200-4060 segment (100% to 90%, 140 mV span) is 95%.
  EXPECT_EQ(app_core::battery_percent(4130), 95);
  // Midpoint of the 3300-3000 segment (5% to 0%, 300 mV span) is ~2-3%.
  const uint8_t mid_low = app_core::battery_percent(3150);
  EXPECT_TRUE(mid_low == 2 || mid_low == 3);
}

HOST_TEST(battery_percent_clamps_outside_the_curve) {
  EXPECT_EQ(app_core::battery_percent(4500), 100);
  EXPECT_EQ(app_core::battery_percent(2900), 0);
  EXPECT_EQ(app_core::battery_percent(0), 0);
}

HOST_TEST(battery_percent_is_monotonic_across_the_full_range) {
  uint8_t previous = app_core::battery_percent(2000);
  for (int mv = 2000; mv <= 4500; mv += 10) {
    const uint8_t current = app_core::battery_percent(mv);
    EXPECT_TRUE(current >= previous);
    previous = current;
  }
}

HOST_TEST(battery_reading_valid_gates_implausible_readings) {
  EXPECT_TRUE(!app_core::battery_reading_valid(0));
  EXPECT_TRUE(!app_core::battery_reading_valid(2499));
  EXPECT_TRUE(app_core::battery_reading_valid(2500));
  EXPECT_TRUE(app_core::battery_reading_valid(3700));
}

HOST_TEST(battery_millivolts_applies_the_three_x_divider) {
  // No calibration trim: 1000 permille is a straight 3x.
  EXPECT_EQ(app_core::battery_millivolts_scaled(1000, 1000), 3000);
  EXPECT_EQ(app_core::battery_millivolts(1000), 3000);
}

HOST_TEST(battery_millivolts_scales_with_calibration_permille_both_ways) {
  // Divider reads low versus a multimeter: trim upward past nominal.
  EXPECT_EQ(app_core::battery_millivolts_scaled(1000, 1100), 3300);
  // Divider reads high versus a multimeter: trim downward past nominal.
  EXPECT_EQ(app_core::battery_millivolts_scaled(1000, 900), 2700);
}

HOST_TEST(battery_overvoltage_warning_is_false_below_and_true_at_and_above_threshold) {
  EXPECT_TRUE(!app_core::battery_overvoltage_warning(4249));
  EXPECT_TRUE(app_core::battery_overvoltage_warning(4250));
  EXPECT_TRUE(app_core::battery_overvoltage_warning(4300));
  EXPECT_TRUE(app_core::battery_overvoltage_warning(5000));
}

HOST_TEST(battery_overvoltage_danger_is_false_below_and_true_at_and_above_threshold) {
  EXPECT_TRUE(!app_core::battery_overvoltage_danger(4299));
  EXPECT_TRUE(app_core::battery_overvoltage_danger(4300));
  EXPECT_TRUE(app_core::battery_overvoltage_danger(5000));
}

HOST_TEST(battery_overvoltage_does_not_fire_on_a_genuinely_full_cell_or_real_hardware_readings) {
  // A full 4200 mV cell is normal, not an overvoltage condition.
  EXPECT_TRUE(!app_core::battery_overvoltage_warning(4200));
  EXPECT_TRUE(!app_core::battery_overvoltage_danger(4200));
  // Real board readings on USB plateaued at 4056-4071 mV; neither threshold
  // should ever fire on values in that range.
  EXPECT_TRUE(!app_core::battery_overvoltage_warning(4071));
  EXPECT_TRUE(!app_core::battery_overvoltage_danger(4071));
}

HOST_TEST(smoothed_battery_millivolts_averages_recent_readings) {
  // The exact jitter observed on real hardware within a couple of minutes:
  // each sample was itself legitimate, but showing every one moved the
  // displayed percent for no real reason.
  const int recent[] = {4078, 4050, 4069};
  EXPECT_EQ(app_core::smoothed_battery_millivolts(recent, 3),
            (4078 + 4050 + 4069) / 3);
}

HOST_TEST(smoothed_battery_millivolts_handles_one_reading_and_none) {
  const int one[] = {3820};
  EXPECT_EQ(app_core::smoothed_battery_millivolts(one, 1), 3820);
  EXPECT_EQ(app_core::smoothed_battery_millivolts(nullptr, 0), 0);
  EXPECT_EQ(app_core::smoothed_battery_millivolts(one, 0), 0);
}

HOST_TEST(smoothed_battery_millivolts_is_order_independent) {
  // A plain average, not a weighted or time-aware one - the caller's ring
  // buffer wraps without preserving insertion order, so the result must not
  // depend on it.
  const int forward[] = {4000, 4100, 4200};
  const int shuffled[] = {4200, 4000, 4100};
  EXPECT_EQ(app_core::smoothed_battery_millivolts(forward, 3),
            app_core::smoothed_battery_millivolts(shuffled, 3));
}

HOST_TEST(voltage_suggests_charging_requires_every_recent_reading_above_threshold) {
  // All at or above kChargingVoltageThresholdMillivolts (4150): sustained.
  const int all_high[] = {4180, 4190, 4200, 4150};
  EXPECT_TRUE(app_core::voltage_suggests_charging(all_high, 4));

  // Exactly one below threshold - even briefly dropping below is not
  // "sustained", the whole point of requiring every recent reading.
  const int one_low[] = {4180, 4190, 4149, 4200};
  EXPECT_TRUE(!app_core::voltage_suggests_charging(one_low, 4));

  // A single high reading (count == 1) is enough evidence for this
  // function - the "not a single sample" caution in its own comment is
  // about the false-positive case, not about needing count > 1 to fire.
  const int single[] = {4150};
  EXPECT_TRUE(app_core::voltage_suggests_charging(single, 1));

  // Normal discharging voltages, nowhere near the threshold.
  const int discharging[] = {3820, 3800, 3790};
  EXPECT_TRUE(!app_core::voltage_suggests_charging(discharging, 3));
}

HOST_TEST(voltage_suggests_charging_handles_the_exact_threshold_and_empty_input) {
  const int exactly_at[] = {app_core::kChargingVoltageThresholdMillivolts};
  EXPECT_TRUE(app_core::voltage_suggests_charging(exactly_at, 1));
  const int one_below[] = {app_core::kChargingVoltageThresholdMillivolts - 1};
  EXPECT_TRUE(!app_core::voltage_suggests_charging(one_below, 1));
  EXPECT_TRUE(!app_core::voltage_suggests_charging(nullptr, 0));
  EXPECT_TRUE(!app_core::voltage_suggests_charging(exactly_at, 0));
}

HOST_TEST(voltage_suggests_charging_honest_false_positive_a_full_cell_just_unplugged) {
  // The exact case the function's own comment names: a genuinely full cell,
  // disconnected moments ago, reads identically to one still charging.
  // Documented behaviour, not a bug - this test exists so a future change
  // that "fixes" it without a real signal to base the fix on gets noticed.
  const int full_cell_off_charger[] = {4180, 4170, 4160};
  EXPECT_TRUE(app_core::voltage_suggests_charging(full_cell_off_charger, 3));
}

// --- ChargeDetector -------------------------------------------------------
//
// The signal voltage_suggests_charging() above cannot produce: whether a
// charger is attached at a state of charge nowhere near full.

HOST_TEST(charge_detector_sees_a_charger_at_any_state_of_charge) {
  app_core::ChargeDetector detector;
  // Half charge, on nothing. 3.85 V is 300 mV below
  // kChargingVoltageThresholdMillivolts, so the high-water rule is blind here.
  for (const int mv : {3850, 3845, 3852, 3848}) {
    EXPECT_TRUE(!detector.update(mv));
    EXPECT_TRUE(!app_core::voltage_suggests_charging(&mv, 1));
  }
  // The cable lands: charge current across the cell's internal resistance
  // steps the terminal voltage up off the valley.
  EXPECT_TRUE(detector.update(3845 + app_core::kBatteryChargeStepMillivolts));
}

HOST_TEST(charge_detector_ignores_jitter_and_a_slow_discharge) {
  app_core::ChargeDetector detector;
  // The spread observed on hardware (4078/4050/4069 mV between consecutive
  // samples), riding a cell that is genuinely draining. Every upward sample
  // here is noise off a falling valley and must not read as a charger.
  for (const int mv : {4078, 4050, 4069, 4040, 4061, 4030, 4051, 4020}) {
    EXPECT_TRUE(!detector.update(mv));
  }
}

HOST_TEST(charge_detector_lets_go_when_the_cable_comes_out) {
  app_core::ChargeDetector detector;
  // First reading only seeds the extreme - one sample is not a direction, and
  // this is what voltage_suggests_charging() is the anchor for.
  EXPECT_TRUE(!detector.update(3900));
  EXPECT_TRUE(detector.update(3900 + app_core::kBatteryChargeStepMillivolts));

  // Climbing, then a dip short of the threshold: still charging. A charging
  // cell's voltage is not monotonic at this resolution.
  EXPECT_TRUE(detector.update(4000));
  EXPECT_TRUE(
      detector.update(4000 - app_core::kBatteryChargeStepMillivolts + 1));
  // The load pulling the terminal voltage down off the peak is an unplug.
  EXPECT_TRUE(!detector.update(4000 - app_core::kBatteryChargeStepMillivolts));
  // ...and it stays that way as the cell drains rather than flapping back.
  for (const int mv : {3930, 3925, 3931, 3920}) {
    EXPECT_TRUE(!detector.update(mv));
  }
}

HOST_TEST(charge_detector_threshold_clears_the_jitter_it_has_to_clear) {
  // The two numbers this detector lives between, asserted rather than
  // trusted: the step has to be bigger than the ADC spread seen on hardware
  // and smaller than what a charger does to the terminal voltage.
  EXPECT_TRUE(app_core::kBatteryChargeStepMillivolts > 30);
  EXPECT_TRUE(app_core::kBatteryChargeStepMillivolts < 150);
}

// Locks down the ownership split app_snapshot.hpp documents on
// AppSnapshot::battery_runtime: wifi_provision's set_battery() (every ~30 s,
// a different task than the ~5 min history/runtime estimator) does exactly
// this - `snapshot_.battery = battery;` - with a freshly sampled, always
// default-constructed-runtime BatteryData. Before battery_runtime moved out
// to its own AppSnapshot field, that whole-struct assignment silently wiped
// whatever set_runtime_estimate() had just published, roughly nine sample
// periods out of every ten (the settings page then read "Collecting"
// almost permanently instead of the real projection). This is the
// regression test for the fix, not for wifi_provision.cpp directly - that
// file is not part of the host build - but the hazard was always in
// whether these two fields share a struct, which is exactly what this
// checks.
HOST_TEST(publishing_a_battery_reading_does_not_erase_a_runtime_estimate) {
  app_core::AppSnapshot snapshot;
  snapshot.battery_runtime.trend = app_core::PowerTrend::Discharging;
  snapshot.battery_runtime.known = true;
  snapshot.battery_runtime.minutes_remaining = 483;
  snapshot.battery_runtime.percent_per_hour = -0.48f;
  snapshot.battery_runtime.samples_used = 129;

  // Exactly what the battery sampler does every ~30 s: build a fresh
  // BatteryData from this reading alone and assign it wholesale.
  app_core::BatteryData fresh_reading;
  fresh_reading.valid = true;
  fresh_reading.millivolts = 3820;
  fresh_reading.percent = 91;
  snapshot.battery = fresh_reading;

  EXPECT_TRUE(snapshot.battery.valid);
  EXPECT_EQ(static_cast<int>(snapshot.battery.percent), 91);
  // The estimate must have survived - there is no longer a shared struct
  // for the battery assignment above to have reached into.
  EXPECT_TRUE(snapshot.battery_runtime.trend ==
              app_core::PowerTrend::Discharging);
  EXPECT_TRUE(snapshot.battery_runtime.known);
  EXPECT_EQ(static_cast<int>(snapshot.battery_runtime.minutes_remaining), 483);
  EXPECT_EQ(static_cast<int>(snapshot.battery_runtime.samples_used), 129);
}
