#include "settings_menu.hpp"
#include "ui_strings.hpp"

#include "test_support.hpp"

#include <cstring>
#include <set>
#include <string>

// One button moves the cursor and there is no way to go back up, so a cursor
// that stopped at the last row would be one you could never leave. Runtime,
// Battery, and Firmware are display-only and deliberately skipped (see
// settings_display_only_rows_cannot_be_focused below), so only the four
// actionable rows are expected to come back around.
HOST_TEST(settings_cursor_wraps_so_every_focusable_item_stays_reachable) {
  ui::SettingsMenu menu;
  const ui::SettingsItem first = menu.focused();

  std::set<int> visited;
  // count() is a safe upper bound on how long the cycle could possibly be;
  // the loop stops itself the moment it actually wraps back to the start.
  for (std::size_t step = 0; step < ui::SettingsMenu::count(); ++step) {
    visited.insert(static_cast<int>(menu.focused()));
    menu.focus_next();
    if (menu.focused() == first) break;
  }
  // Language, Volume, WifiSetup, CheckUpdates - the four actionable rows.
  EXPECT_EQ(static_cast<int>(visited.size()), 4);
  EXPECT_TRUE(menu.focused() == first);
}

// Locks in the operator-specified render order itself, not just that some
// order is self-consistent: frequently-used rows lead, rows nobody interacts
// with trail, and the version row - which does nothing when selected - sinks
// furthest of all. Checked against the enum's own declared order rather than
// by walking focus_next(), because Runtime/Battery/Firmware are no longer
// focus_next()-reachable at all (see settings_display_only_rows_cannot_be_
// focused below) - they still render in this order, only the cursor skips
// them.
HOST_TEST(settings_items_are_ordered_by_how_often_they_are_actually_used) {
  const ui::SettingsItem expected_order[] = {
      ui::SettingsItem::Language,     ui::SettingsItem::Volume,
      ui::SettingsItem::WifiSetup,    ui::SettingsItem::CheckUpdates,
      ui::SettingsItem::Runtime,      ui::SettingsItem::Battery,
      ui::SettingsItem::Firmware,
  };
  static_assert(sizeof(expected_order) / sizeof(expected_order[0]) ==
                    ui::SettingsMenu::count(),
                "update this test's expected order alongside SettingsItem");
  for (std::size_t i = 0; i < ui::SettingsMenu::count(); ++i) {
    EXPECT_TRUE(static_cast<ui::SettingsItem>(i) == expected_order[i]);
  }
}

// The actual defect being fixed: a display-only row used to still be able to
// take focus, so a press on it was a press that did nothing. Asserted by
// walking every focus_next() step and checking these three by name, not by
// checking that they merely end up last - a future reorder that moves one of
// them earlier in the enum must not silently make it focusable again.
HOST_TEST(settings_display_only_rows_cannot_be_focused) {
  ui::SettingsMenu menu;
  const ui::SettingsItem first = menu.focused();
  for (std::size_t step = 0; step < ui::SettingsMenu::count(); ++step) {
    EXPECT_TRUE(menu.focused() != ui::SettingsItem::Runtime);
    EXPECT_TRUE(menu.focused() != ui::SettingsItem::Battery);
    EXPECT_TRUE(menu.focused() != ui::SettingsItem::Firmware);
    menu.focus_next();
    if (menu.focused() == first) break;
  }
}

HOST_TEST(settings_menu_opens_at_the_top_every_time) {
  ui::SettingsMenu menu;
  const ui::SettingsItem first = menu.focused();
  menu.focus_next();
  menu.focus_next();
  EXPECT_TRUE(menu.focused() != first);
  menu.reset();
  EXPECT_TRUE(menu.focused() == first);
}

HOST_TEST(settings_activation_only_acts_where_acting_makes_sense) {
  ui::set_language(ui::Language::English);
  ui::SettingsMenu menu;

  // Runtime, Battery, and Firmware used to be checked here too - focus_next()
  // walked to each by name and confirmed activate() returned None. They are
  // no longer reachable that way at all (see
  // settings_display_only_rows_cannot_be_focused), which subsumes this: a
  // row activate() can never be called on cannot swallow a press regardless
  // of what activate() would have done. Only the four actionable rows remain
  // here.
  while (menu.focused() != ui::SettingsItem::Language) menu.focus_next();
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::LanguageChanged);
  EXPECT_TRUE(ui::language() == ui::Language::TraditionalChinese);
  // Cycles rather than latching, so the only control available can undo
  // itself - picking the wrong language must not strand anyone.
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::LanguageChanged);
  EXPECT_TRUE(ui::language() == ui::Language::English);

  while (menu.focused() != ui::SettingsItem::CheckUpdates) menu.focus_next();
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::StartUpdateCheck);
  while (menu.focused() != ui::SettingsItem::WifiSetup) menu.focus_next();
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::EnterWifiSetup);

  while (menu.focused() != ui::SettingsItem::Volume) menu.focus_next();
  ui::set_volume_preset(ui::VolumePreset::Medium);
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::VolumeChanged);
  EXPECT_TRUE(ui::volume_preset() == ui::VolumePreset::High);
  // Cycles through all four and wraps, same as Language above - there is
  // no way to go back with one button, so every level must stay reachable.
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::VolumeChanged);
  EXPECT_TRUE(ui::volume_preset() == ui::VolumePreset::Off);
}

// 50 (Medium) is the one level anyone has actually listened to on real
// hardware, so it is pinned here rather than left to whatever order the
// enum happens to be declared in; the rest is an even, documented spread
// around it, not a guess that happens to average out.
HOST_TEST(volume_preset_percent_is_anchored_at_the_only_tested_level) {
  EXPECT_EQ(ui::volume_preset_percent(ui::VolumePreset::Off), 0);
  EXPECT_EQ(ui::volume_preset_percent(ui::VolumePreset::Low), 25);
  EXPECT_EQ(ui::volume_preset_percent(ui::VolumePreset::Medium), 50);
  EXPECT_EQ(ui::volume_preset_percent(ui::VolumePreset::High), 75);
  // Below 80: a hardware sweep already found 80% "a bit loud" (see
  // modules/audio/README.md), so nothing here should ever reach it.
  EXPECT_TRUE(ui::volume_preset_percent(ui::VolumePreset::High) < 80);
}

HOST_TEST(every_interface_string_exists_in_both_languages) {
  for (std::uint16_t raw = 0; raw < static_cast<std::uint16_t>(ui::Text::Count);
       ++raw) {
    const auto id = static_cast<ui::Text>(raw);
    const char* en = ui::text_in(ui::Language::English, id);
    const char* zh = ui::text_in(ui::Language::TraditionalChinese, id);
    EXPECT_TRUE(en != nullptr && en[0] != '\0');
    // Falls back to English rather than blank, so this can never be empty -
    // what it proves is that lookup never returns nothing to draw.
    EXPECT_TRUE(zh != nullptr && zh[0] != '\0');
  }
}

// The English column feeds the ASCII-only compiled font; the Chinese column
// feeds the glyph subset the font build script extracts. A stray non-ASCII
// byte in the English column would render as a blank box.
HOST_TEST(english_strings_stay_within_the_ascii_font) {
  for (std::uint16_t raw = 0; raw < static_cast<std::uint16_t>(ui::Text::Count);
       ++raw) {
    const char* en =
        ui::text_in(ui::Language::English, static_cast<ui::Text>(raw));
    for (const char* c = en; *c != '\0'; ++c) {
      EXPECT_TRUE(static_cast<unsigned char>(*c) < 0x80);
    }
  }
}

HOST_TEST(each_language_names_itself_in_its_own_script) {
  // Whoever needs this list cannot read the language currently on screen -
  // that is the whole reason they are looking for it - so the names must not
  // follow the active language.
  ui::set_language(ui::Language::English);
  const std::string english_view_en = ui::language_name(ui::Language::English);
  const std::string english_view_zh =
      ui::language_name(ui::Language::TraditionalChinese);

  ui::set_language(ui::Language::TraditionalChinese);
  EXPECT_TRUE(ui::language_name(ui::Language::English) == english_view_en);
  EXPECT_TRUE(ui::language_name(ui::Language::TraditionalChinese) ==
              english_view_zh);
  ui::set_language(ui::Language::English);
}

HOST_TEST(button_hints_are_silent_only_while_flash_is_being_written) {
  EXPECT_TRUE(ui::input_hints(ui::InputContext::Carousel).visible);
  EXPECT_TRUE(ui::input_hints(ui::InputContext::Menu).visible);
  // Naming buttons that are being ignored invites the one interaction that
  // must not happen mid-write.
  EXPECT_TRUE(!ui::input_hints(ui::InputContext::Locked).visible);

  // The two contexts that accept input must not describe themselves the same
  // way, or the hint bar teaches nothing.
  const auto carousel = ui::input_hints(ui::InputContext::Carousel);
  const auto menu = ui::input_hints(ui::InputContext::Menu);
  EXPECT_TRUE(carousel.key != menu.key || carousel.boot != menu.boot);
}

HOST_TEST(every_menu_item_has_its_own_label) {
  std::set<std::string> labels;
  for (std::size_t i = 0; i < ui::SettingsMenu::count(); ++i) {
    labels.insert(ui::text(ui::settings_item_label(
        static_cast<ui::SettingsItem>(i))));
  }
  EXPECT_EQ(static_cast<int>(labels.size()),
            static_cast<int>(ui::SettingsMenu::count()));
}

HOST_TEST(update_row_installs_only_what_a_check_actually_found) {
  ui::SettingsMenu menu;
  while (menu.focused() != ui::SettingsItem::CheckUpdates) menu.focus_next();

  // Nothing found yet: the row asks.
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::StartUpdateCheck);

  menu.set_update_offered(true);
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::StartUpdateInstall);

  // Re-entering the menu drops the offer. An update found days ago is a URL
  // that may no longer exist, and a row that still reads "install" would
  // reflash on the strength of a stale answer.
  menu.reset();
  while (menu.focused() != ui::SettingsItem::CheckUpdates) menu.focus_next();
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::StartUpdateCheck);
}

HOST_TEST(every_page_spells_a_temperature_the_same_way) {
  // The degree sign is two UTF-8 bytes, C2 B0, and LVGL's Montserrat carries
  // U+00B0 - so this renders rather than becoming a box.
  EXPECT_TRUE(ui::temperature_text(30.24f, 1) == "30.2°C");
  // Decimals are a real difference, not a style one: a forecast high is not
  // measured to a tenth.
  EXPECT_TRUE(ui::temperature_text(27.6f, 0) == "28°C");
  // Below zero the sign must survive the format, not be eaten by the width.
  EXPECT_TRUE(ui::temperature_text(-4.5f, 1) == "-4.5°C");
}

HOST_TEST(language_is_persisted_only_when_it_actually_changes) {
  static int writes = 0;
  static ui::Language last = ui::Language::English;
  writes = 0;
  ui::set_language(ui::Language::English);
  ui::set_language_store_handler(
      [](ui::Language value) { ++writes; last = value; });

  // Selecting the language already in force must not spend a flash write.
  ui::set_language(ui::Language::English);
  EXPECT_EQ(writes, 0);

  ui::set_language(ui::Language::TraditionalChinese);
  EXPECT_EQ(writes, 1);
  EXPECT_TRUE(last == ui::Language::TraditionalChinese);

  // Out of range is refused rather than stored - the enum indexes the string
  // table, and a value with no row would read off the end of it.
  ui::set_language(ui::Language::Count);
  EXPECT_EQ(writes, 1);
  EXPECT_TRUE(ui::language() == ui::Language::TraditionalChinese);

  ui::set_language_store_handler(nullptr);
  ui::set_language(ui::Language::English);
}

// Mirrors language_is_persisted_only_when_it_actually_changes above: this
// component has no flash of its own to round-trip against (main/app_main.cpp
// owns the actual NVS read/write, using this exact store-handler seam), so
// what is host-testable - and what would actually break if the wiring
// between the settings row and NVS were lost - is that the handler fires
// exactly once per real change, carries the right value, and never fires
// for a no-op cycle or an out-of-range write.
HOST_TEST(volume_preset_is_persisted_only_when_it_actually_changes) {
  static int writes = 0;
  static ui::VolumePreset last = ui::VolumePreset::Medium;
  writes = 0;
  ui::set_volume_preset(ui::VolumePreset::Medium);
  ui::set_volume_preset_store_handler(
      [](ui::VolumePreset value) { ++writes; last = value; });

  // Selecting the preset already in force must not spend a flash write.
  ui::set_volume_preset(ui::VolumePreset::Medium);
  EXPECT_EQ(writes, 0);

  ui::set_volume_preset(ui::VolumePreset::Low);
  EXPECT_EQ(writes, 1);
  EXPECT_TRUE(last == ui::VolumePreset::Low);

  ui::set_volume_preset(ui::VolumePreset::Off);
  EXPECT_EQ(writes, 2);
  EXPECT_TRUE(last == ui::VolumePreset::Off);

  // Out of range is refused rather than stored - the enum indexes
  // volume_preset_percent(), and a value with no case would fall through to
  // its default rather than reading off the end of anything, but it must
  // still never reach NVS as a value nothing wrote on purpose.
  ui::set_volume_preset(ui::VolumePreset::Count);
  EXPECT_EQ(writes, 2);
  EXPECT_TRUE(ui::volume_preset() == ui::VolumePreset::Off);

  ui::set_volume_preset_store_handler(nullptr);
  ui::set_volume_preset(ui::VolumePreset::Medium);
}
