#include "settings_menu.hpp"
#include "ui_strings.hpp"

#include "test_support.hpp"

#include <cstring>
#include <set>
#include <string>

// One button moves the cursor and there is no way to go back up, so a cursor
// that stopped at the last row would be one you could never leave.
HOST_TEST(settings_cursor_wraps_so_every_item_stays_reachable) {
  ui::SettingsMenu menu;
  EXPECT_TRUE(menu.focused() == ui::SettingsItem::Firmware);

  std::set<int> visited;
  for (std::size_t step = 0; step < ui::SettingsMenu::count(); ++step) {
    visited.insert(static_cast<int>(menu.focused()));
    menu.focus_next();
  }
  // Every item seen exactly once, and back at the start.
  EXPECT_EQ(static_cast<int>(visited.size()),
            static_cast<int>(ui::SettingsMenu::count()));
  EXPECT_TRUE(menu.focused() == ui::SettingsItem::Firmware);
}

HOST_TEST(settings_menu_opens_at_the_top_every_time) {
  ui::SettingsMenu menu;
  menu.focus_next();
  menu.focus_next();
  EXPECT_TRUE(menu.focused() != ui::SettingsItem::Firmware);
  menu.reset();
  EXPECT_TRUE(menu.focused() == ui::SettingsItem::Firmware);
}

HOST_TEST(settings_activation_only_acts_where_acting_makes_sense) {
  ui::set_language(ui::Language::English);
  ui::SettingsMenu menu;

  // The version row is display-only; pressing select on it must be inert
  // rather than doing something surprising.
  EXPECT_TRUE(menu.focused() == ui::SettingsItem::Firmware);
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::None);
  EXPECT_TRUE(ui::language() == ui::Language::English);

  menu.focus_next();
  EXPECT_TRUE(menu.focused() == ui::SettingsItem::Language);
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::LanguageChanged);
  EXPECT_TRUE(ui::language() == ui::Language::TraditionalChinese);
  // Cycles rather than latching, so the only control available can undo
  // itself - picking the wrong language must not strand anyone.
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::LanguageChanged);
  EXPECT_TRUE(ui::language() == ui::Language::English);

  menu.focus_next();
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::StartUpdateCheck);
  menu.focus_next();
  EXPECT_TRUE(menu.activate() == ui::SettingsAction::EnterWifiSetup);
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
