#define UI_THEME_GEOMETRY_ONLY
#include "app_snapshot.hpp"
#include "page_registry.hpp"
#include "ui_data.hpp"

#include "test_support.hpp"

#include <algorithm>

namespace {

bool is_printable_ascii(const char* text) {
  for (const unsigned char* cursor =
           reinterpret_cast<const unsigned char*>(text);
       *cursor != '\0'; ++cursor) {
    if (*cursor < 0x20 || *cursor > 0x7e) return false;
  }
  return true;
}

bool rects_overlap(const ui::Rect a, const ui::Rect b) {
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() &&
        b.y < a.bottom();
}

}  // namespace

HOST_TEST(setup_page_is_excluded_from_registry_and_page_count_stays_five) {
  app_core::AppSnapshot snapshot =
      app_core::make_mock_snapshot(app_core::DemoScenario::TaiwanSession);
  snapshot.setup.active = true;
  app_core::PageRegistry registry;
  registry.begin_cycle(snapshot);
  EXPECT_EQ(registry.size(), static_cast<std::size_t>(5));
  for (const app_core::PageId page : registry.page_ids()) {
    EXPECT_TRUE(page != app_core::PageId::Setup);
  }
}

HOST_TEST(setup_layout_qr_and_text_fit_inside_the_safe_canvas) {
  const ui::SetupLayout layout = ui::setup_layout(ui::safe_canvas());
  EXPECT_TRUE(ui::within_safe_canvas(layout.qr));
  EXPECT_TRUE(ui::within_safe_canvas(layout.title));
  EXPECT_TRUE(ui::within_safe_canvas(layout.ssid));
  EXPECT_TRUE(ui::within_safe_canvas(layout.instructions));
  EXPECT_TRUE(ui::within_safe_canvas(layout.status));
}

HOST_TEST(setup_layout_qr_and_text_column_never_overlap) {
  const ui::SetupLayout layout = ui::setup_layout(ui::safe_canvas());
  EXPECT_TRUE(!rects_overlap(layout.qr, layout.title));
  EXPECT_TRUE(!rects_overlap(layout.qr, layout.ssid));
  EXPECT_TRUE(!rects_overlap(layout.qr, layout.instructions));
  EXPECT_TRUE(!rects_overlap(layout.qr, layout.status));
  EXPECT_TRUE(!rects_overlap(layout.title, layout.ssid));
  EXPECT_TRUE(!rects_overlap(layout.ssid, layout.instructions));
  EXPECT_TRUE(!rects_overlap(layout.instructions, layout.status));
}

HOST_TEST(setup_page_strings_are_ascii_only) {
  EXPECT_TRUE(is_printable_ascii(ui::kSetupTitle));
  EXPECT_TRUE(is_printable_ascii(ui::kSetupNoSsidLabel));
  EXPECT_TRUE(is_printable_ascii(ui::kSetupInstructions));
  EXPECT_TRUE(is_printable_ascii(ui::kSetupDefaultStatus));
  EXPECT_TRUE(is_printable_ascii(ui::kSetupQrUnavailableLabel));
}
