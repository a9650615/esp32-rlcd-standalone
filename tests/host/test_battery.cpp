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

// The charger offset, which is what lets a percentage be shown during a
// charge at all instead of being withheld for hours.
HOST_TEST(charge_offset_takes_either_cable_edge_and_clamps_a_glitch) {
  // The measured step on this board, both ways round: 4202 -> 4140 mV
  // unplugging a full cell. Sign carries which edge it was; the offset is
  // the same 62 mV either way.
  EXPECT_EQ(app_core::charge_offset_from_cable_step(62), 62);
  EXPECT_EQ(app_core::charge_offset_from_cable_step(-62), 62);
  // Not a cable: an ADC glitch or a brownout must not be able to subtract
  // an arbitrary amount from the displayed level.
  EXPECT_EQ(app_core::charge_offset_from_cable_step(900),
            app_core::kChargeOffsetMaxMillivolts);
  EXPECT_EQ(app_core::charge_offset_from_cable_step(-900),
            app_core::kChargeOffsetMaxMillivolts);
  EXPECT_EQ(app_core::charge_offset_from_cable_step(0), 0);
}

HOST_TEST(open_circuit_voltage_is_corrected_only_while_charging) {
  // Not charging: the reading is the cell's own voltage already, and
  // subtracting anything from it would invent a discharge nobody measured.
  EXPECT_EQ(app_core::battery_open_circuit_millivolts(3850, 62, false), 3850);
  // Charging: the charger's contribution comes back out.
  EXPECT_EQ(app_core::battery_open_circuit_millivolts(3850, 62, true), 3788);
  // Booted already sitting on a charger, so no cable event was ever seen:
  // nothing measured, nothing subtracted. The percentage reads a few points
  // high, which is the honest outcome, not a guessed constant.
  EXPECT_EQ(app_core::battery_open_circuit_millivolts(3850, 0, true), 3850);
  // Never negative, whatever the offset and reading are.
  EXPECT_EQ(app_core::battery_open_circuit_millivolts(40, 120, true), 0);
}

// The point of the whole correction, stated in the units the panel shows:
// through the knee of the discharge curve, a charger's offset is worth
// double-digit percentage points, which is why the uncorrected number was
// judged not worth showing at all.
HOST_TEST(the_charge_offset_is_worth_double_digit_percentage_points) {
  const int measured_mv = 3850;   // on the charger
  const int offset_mv = 62;       // this board's measured cable step
  const uint8_t uncorrected = app_core::battery_percent(measured_mv);
  const uint8_t corrected = app_core::battery_percent(
      app_core::battery_open_circuit_millivolts(measured_mv, offset_mv, true));
  EXPECT_TRUE(corrected < uncorrected);
  EXPECT_TRUE(uncorrected - corrected >= 10);
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

// The 69 readings measured on hardware over 34.5 minutes with the USB cable
// out, oldest first, 30 s apart. Slope is -0.66 mV/min = -40 mV/hour, and the
// whole series sits above kChargingVoltageThresholdMillivolts - which is
// exactly the case that showed a charging icon for an hour after unplugging.
constexpr int kMeasuredDischarge[] = {
    4214, 4211, 4208, 4199, 4205, 4208, 4199, 4202, 4193, 4202, 4205, 4217,
    4205, 4211, 4208, 4208, 4214, 4202, 4205, 4211, 4208, 4199, 4211, 4205,
    4205, 4208, 4214, 4205, 4193, 4193, 4202, 4214, 4211, 4205, 4196, 4205,
    4193, 4196, 4196, 4193, 4190, 4196, 4193, 4202, 4196, 4190, 4183, 4196,
    4193, 4196, 4193, 4186, 4193, 4180, 4199, 4183, 4193, 4190, 4183, 4199,
    4190, 4186, 4196, 4180, 4190, 4186, 4193, 4186, 4196};
constexpr int kMeasuredCount =
    static_cast<int>(sizeof(kMeasuredDischarge) / sizeof(int));

HOST_TEST(voltage_is_falling_recognises_the_measured_discharge) {
  // The whole real series: 69 samples 30 s apart is a 34-minute span, well
  // past kChargingSlopeMinSpanSeconds. This is the case the fix exists for.
  EXPECT_TRUE(app_core::voltage_is_falling(kMeasuredDischarge, kMeasuredCount, 30));

  // And the level alone does not, which is the whole point: every reading in
  // that series is above the charging threshold, so the old signal calls it
  // charging for the entire 34 minutes.
  EXPECT_TRUE(
      app_core::voltage_suggests_charging(kMeasuredDischarge, kMeasuredCount));
}

HOST_TEST(a_full_slope_window_at_the_samplers_cadence_is_measurable) {
  // The regression test for a five-second off-by-one. The window and the
  // minimum span are two constants that have to agree, and they disagreed:
  // 132 samples 5 s apart span 655 s against a 660 s minimum, so a full
  // window was refused and both direction signals were permanently false.
  // Nothing failed visibly - the CV test below passed on the refusal instead
  // of on the fit - so this asserts the span itself, in the units the
  // sampler feeds.
  EXPECT_TRUE((app_core::kChargingSlopeWindow - 1) * 5 >=
              app_core::kChargingSlopeMinSpanSeconds);

  // And that a real discharge over exactly that window is called: 132
  // intervals of -1.7 mV each is the -20 mV/hour boundary crossed with room
  // to spare, and it must not be refused for span.
  int falling[app_core::kChargingSlopeWindow];
  for (int i = 0; i < app_core::kChargingSlopeWindow; ++i) {
    falling[i] = 4205 - i / 3;
  }
  EXPECT_TRUE(
      app_core::voltage_is_falling(falling, app_core::kChargingSlopeWindow, 5));
}

HOST_TEST(voltage_is_falling_says_no_to_a_charger_holding_cv) {
  // A charger at its CV setpoint holds the terminal voltage, so the only
  // movement is ADC noise. Same +/-10 mV seen on hardware, no trend. Sized
  // and spaced the way the sampler actually feeds it: 132 samples, 5 s apart.
  int held[app_core::kChargingSlopeWindow];
  for (int i = 0; i < app_core::kChargingSlopeWindow; ++i) {
    held[i] = 4205 + ((i % 3) - 1) * 9;
  }
  EXPECT_TRUE(
      !app_core::voltage_is_falling(held, app_core::kChargingSlopeWindow, 5));
}

HOST_TEST(the_rule_is_span_not_sample_count) {
  // The same forty real samples, read two ways. Fitted slopes, computed from
  // this array:
  //
  //   40 samples at 30 s -> 1170 s span, -44.7 mV/hour -> falling
  //   40 samples at  5 s ->  195 s span, six times steeper per hour, refused
  //
  // The second is refused by the span guard even though its per-hour slope is
  // larger, which is the point: a rule written in sample count would have
  // accepted it. Precision comes from how long you watched, not how often you
  // looked.
  const int* window = kMeasuredDischarge + (kMeasuredCount - 40);
  EXPECT_TRUE(app_core::voltage_is_falling(window, 40, 30));
  EXPECT_TRUE(!app_core::voltage_is_falling(window, 40, 5));
}

HOST_TEST(ten_minutes_of_the_real_series_is_not_enough_to_call_it) {
  // Why eleven minutes is a measurement and not a preference.
  //
  // Twenty samples spaced 40 s apart clears the span guard at 760 s, so the
  // guard is not what answers here - the fit is. Ten minutes of this genuine
  // discharge fits to -6.4 mV/hour, well inside the -20 boundary, because
  // -0.66 mV/min over that span is 6.6 mV of movement buried in +/-10 mV of
  // per-reading noise. The same series over 34 minutes fits to -40.7.
  //
  // If a future change shortens kChargingSlopeMinSpanSeconds, this is the test
  // that should stop it.
  EXPECT_TRUE(!app_core::voltage_is_falling(
      kMeasuredDischarge + (kMeasuredCount - 20), 20, 40));
}

HOST_TEST(voltage_is_falling_guards_its_arguments) {
  EXPECT_TRUE(!app_core::voltage_is_falling(nullptr, 40, 30));
  EXPECT_TRUE(!app_core::voltage_is_falling(kMeasuredDischarge, 40, 0));
}

// --- voltage_is_rising ----------------------------------------------------
//
// The half neither of the two above can answer: a cell nowhere near full,
// with a charger attached. Everything up here reads 4180-4217 mV; a cell at
// half charge on a charger reads 3850, and the level rule is blind to it.

HOST_TEST(voltage_is_rising_sees_a_charger_below_the_level_threshold) {
  // A cell climbing through the plateau at +120 mV/hour - the slowest part of
  // a real charge - sampled the way the sampler feeds it: 132 samples, 5 s
  // apart, 11 minutes, so about 22 mV of movement. Noise is the same +/-9 mV
  // the measured series carries.
  int climbing[app_core::kChargingSlopeWindow];
  for (int i = 0; i < app_core::kChargingSlopeWindow; ++i) {
    const double minutes = i * 5.0 / 60.0;
    climbing[i] = 3850 + static_cast<int>(minutes * 2.0) + ((i % 3) - 1) * 9;
  }
  EXPECT_TRUE(
      app_core::voltage_is_rising(climbing, app_core::kChargingSlopeWindow, 5));
  // Which is the whole reason this exists: the level rule calls this
  // discharging for the entire climb.
  EXPECT_TRUE(!app_core::voltage_suggests_charging(
      climbing, app_core::kChargingSlopeWindow));
  EXPECT_TRUE(
      !app_core::voltage_is_falling(climbing, app_core::kChargingSlopeWindow, 5));
}

HOST_TEST(voltage_is_rising_says_no_to_the_measured_discharge_and_to_noise) {
  // The real 34-minute discharge must never read as rising.
  EXPECT_TRUE(
      !app_core::voltage_is_rising(kMeasuredDischarge, kMeasuredCount, 30));

  // Nor may noise alone. Same +/-9 mV, no trend: the bar sits at nearly four
  // sigma of the noise floor precisely so this cannot flip the icon.
  int held[app_core::kChargingSlopeWindow];
  for (int i = 0; i < app_core::kChargingSlopeWindow; ++i) {
    held[i] = 3850 + ((i % 3) - 1) * 9;
  }
  EXPECT_TRUE(
      !app_core::voltage_is_rising(held, app_core::kChargingSlopeWindow, 5));
}

HOST_TEST(voltage_is_rising_refuses_a_span_it_cannot_measure) {
  // Same span rule as falling, and the same reason: "unknown" must not read
  // as "rising" either, or every boot spent on a charger would raise the icon
  // from noise before the window has filled.
  int climbing[40];
  for (int i = 0; i < 40; ++i) climbing[i] = 3850 + i;
  EXPECT_TRUE(!app_core::voltage_is_rising(climbing, 40, 5));  // 195 s span
  EXPECT_TRUE(app_core::voltage_is_rising(climbing, 40, 30));  // 1170 s span
  EXPECT_TRUE(!app_core::voltage_is_rising(nullptr, 40, 30));
  EXPECT_TRUE(!app_core::voltage_is_rising(climbing, 40, 0));
}

HOST_TEST(the_two_thresholds_are_one_fit_read_two_ways) {
  // battery_voltage_slope() is what both read, and the boundaries have to
  // straddle zero with a dead band in between - a cell cannot be rising and
  // falling at once, and a flat one must be neither.
  EXPECT_TRUE(app_core::kDischargeSlopeMillivoltsPerHour < 0.0f);
  EXPECT_TRUE(app_core::kChargingRiseMillivoltsPerHour > 0.0f);

  float per_hour = 0.0f;
  EXPECT_TRUE(app_core::battery_voltage_slope(kMeasuredDischarge,
                                              kMeasuredCount, 30, &per_hour));
  // The measured -40 mV/hour, from the series the thresholds were sized on.
  EXPECT_TRUE(per_hour < -30.0f && per_hour > -50.0f);

  // Refused spans leave the caller's value alone rather than reporting zero,
  // which would read as a flat cell.
  float untouched = 12345.0f;
  EXPECT_TRUE(!app_core::battery_voltage_slope(kMeasuredDischarge, 20, 5,
                                               &untouched));
  EXPECT_EQ(untouched, 12345.0f);
  EXPECT_TRUE(!app_core::battery_voltage_slope(kMeasuredDischarge,
                                               kMeasuredCount, 30, nullptr));
}
