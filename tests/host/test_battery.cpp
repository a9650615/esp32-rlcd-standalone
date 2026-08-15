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
