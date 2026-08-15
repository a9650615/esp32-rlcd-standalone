#define UI_THEME_GEOMETRY_ONLY
#include "app_snapshot.hpp"
#include "ota_decision.hpp"
#include "ui_data.hpp"

#include "test_support.hpp"

// The two decisions that are expensive to get wrong: marking an image valid is
// irreversible for that boot, and rolling one back costs a reboot and drops the
// user onto older firmware.
HOST_TEST(ota_rollback_decision_acts_only_on_a_pending_image) {
  using ota::RollbackDecision;
  using ota::rollback_decision;

  // Pending and demonstrably alive: accept it.
  EXPECT_TRUE(rollback_decision(true, true, true) ==
              RollbackDecision::MarkValid);
  // Pending with no sign of life: this board's watchdog will not reset for us,
  // so the guard has to force the rollback itself.
  EXPECT_TRUE(rollback_decision(true, true, false) ==
              RollbackDecision::Rollback);
  // Not pending: the steady state on every factory boot and every already
  // confirmed slot. Liveness must not drag it into either action.
  EXPECT_TRUE(rollback_decision(true, false, true) == RollbackDecision::None);
  EXPECT_TRUE(rollback_decision(true, false, false) == RollbackDecision::None);
  // State unreadable: stay inert whatever the liveness sample said, rather
  // than guessing at a partition whose state could not be queried.
  EXPECT_TRUE(rollback_decision(false, true, false) == RollbackDecision::None);
  EXPECT_TRUE(rollback_decision(false, true, true) == RollbackDecision::None);
}

HOST_TEST(ota_page_owns_the_screen_only_while_flash_is_being_written) {
  app_core::OtaData ota;
  EXPECT_TRUE(!app_core::ota_owns_screen(ota));  // Idle

  ota.phase = app_core::OtaPhase::Receiving;
  EXPECT_TRUE(app_core::ota_owns_screen(ota));
  ota.phase = app_core::OtaPhase::Writing;
  EXPECT_TRUE(app_core::ota_owns_screen(ota));

  // Verifying is a normal boot that ends on its own; locking the user out of
  // the carousel for it would be a 30-second blackout on every update.
  ota.phase = app_core::OtaPhase::Verifying;
  EXPECT_TRUE(!app_core::ota_owns_screen(ota));
  // RolledBack and Failed are reports, not operations in progress.
  ota.phase = app_core::OtaPhase::RolledBack;
  EXPECT_TRUE(!app_core::ota_owns_screen(ota));
  ota.phase = app_core::OtaPhase::Failed;
  EXPECT_TRUE(!app_core::ota_owns_screen(ota));
}

HOST_TEST(ota_phase_labels_are_ascii_and_present_for_every_visible_phase) {
  // The compiled Montserrat font has no glyphs beyond ASCII, and an
  // out-of-range byte renders as a blank box rather than failing loudly.
  const app_core::OtaPhase phases[] = {
      app_core::OtaPhase::Receiving, app_core::OtaPhase::Writing,
      app_core::OtaPhase::Verifying, app_core::OtaPhase::RolledBack,
      app_core::OtaPhase::Failed};
  for (const app_core::OtaPhase phase : phases) {
    const char* label = app_core::ota_phase_label(phase);
    EXPECT_TRUE(label[0] != '\0');
    for (const char* c = label; *c != '\0'; ++c) {
      EXPECT_TRUE(static_cast<unsigned char>(*c) < 0x80);
    }
  }
  // Idle is the one phase with nothing to say.
  EXPECT_EQ(app_core::ota_phase_label(app_core::OtaPhase::Idle)[0], '\0');
}

HOST_TEST(ota_layout_fits_and_does_not_overlap) {
  const ui::Rect content =
      ui::content_bounds(ui::safe_canvas(), app_core::PageId::Ota);
  EXPECT_TRUE(ui::ota_layout_fits(content));

  const ui::OtaLayout layout = ui::ota_layout(content);
  EXPECT_TRUE(!ui::rects_intersect(layout.phase, layout.percent));
  EXPECT_TRUE(!ui::rects_intersect(layout.percent, layout.warning));
  EXPECT_TRUE(!ui::rects_intersect(layout.warning, layout.detail));

  // The OTA page carries no tray, so it must be given the whole canvas rather
  // than the tray-reduced area every other non-Home page gets.
  EXPECT_EQ(content.height, ui::safe_canvas().height);
}
