#define UI_THEME_GEOMETRY_ONLY
#include "app_snapshot.hpp"
#include "ota_decision.hpp"
#include "ota_image.hpp"
#include "ota_notes.hpp"
#include "ota_prefix.hpp"
#include "ota_version.hpp"
#include "ui_data.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

// A byte-for-byte stand-in for the prefix of a real image, with the same field
// offsets that were read back out of build/layout_carousel.bin.
std::vector<uint8_t> valid_prefix(const char* project = "layout_carousel",
                                  const char* version = "0.1.0") {
  std::vector<uint8_t> image(ota::kImagePrefixBytes, 0);
  image[0] = 0xE9;                              // esp image magic
  image[12] = 0x09;                             // chip id ESP32-S3, LE
  image[13] = 0x00;
  image[32] = 0x32;                             // 0xABCD5432, LE
  image[33] = 0x54;
  image[34] = 0xCD;
  image[35] = 0xAB;
  std::memcpy(image.data() + 48, version, std::strlen(version));
  std::memcpy(image.data() + 80, project, std::strlen(project));
  return image;
}

}  // namespace

// The two decisions that are expensive to get wrong: marking an image valid is
// irreversible for that boot, and rolling one back costs a reboot and drops the
// user onto older firmware.
HOST_TEST(ota_rollback_decision_acts_only_on_a_pending_image) {
  using ota::RollbackDecision;
  using ota::rollback_decision;

  // Pending, drawing, and reachable: accept it.
  EXPECT_TRUE(rollback_decision(true, true, true, true) ==
              RollbackDecision::MarkValid);
  // Pending with no sign of life: this board's watchdog will not reset for us,
  // so the guard has to force the rollback itself.
  EXPECT_TRUE(rollback_decision(true, true, false, true) ==
              RollbackDecision::Rollback);
  // Draws perfectly and cannot be reached. This is the one that matters for a
  // board with no cable attached: accepting it means nothing can ever talk to
  // it again, and the only recovery is USB. Rolling back costs a re-push.
  EXPECT_TRUE(rollback_decision(true, true, true, false) ==
              RollbackDecision::Rollback);
  EXPECT_TRUE(rollback_decision(true, true, false, false) ==
              RollbackDecision::Rollback);
  // Not pending: the steady state on every factory boot and every already
  // confirmed slot. Neither piece of evidence may drag it into an action -
  // in particular a board deliberately run with no Wi-Fi must not roll back,
  // and it never reaches this code because it is not pending in the first
  // place.
  EXPECT_TRUE(rollback_decision(true, false, true, true) ==
              RollbackDecision::None);
  EXPECT_TRUE(rollback_decision(true, false, false, false) ==
              RollbackDecision::None);
  EXPECT_TRUE(rollback_decision(true, false, true, false) ==
              RollbackDecision::None);
  // State unreadable: stay inert whatever the evidence said, rather than
  // guessing at a partition whose state could not be queried.
  EXPECT_TRUE(rollback_decision(false, true, false, false) ==
              RollbackDecision::None);
  EXPECT_TRUE(rollback_decision(false, true, true, true) ==
              RollbackDecision::None);
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

HOST_TEST(ota_image_accepts_this_projects_own_image_shape) {
  const std::vector<uint8_t> image = valid_prefix();
  const ota::ImageInfo info =
      ota::inspect_image_prefix(image.data(), image.size());
  EXPECT_TRUE(info.verdict == ota::ImageVerdict::Ok);
  EXPECT_TRUE(info.version == "0.1.0");
  EXPECT_TRUE(info.project_name == "layout_carousel");
}

// The case this validator exists for: someone picks the wrong file in the
// browser. It must die on the first byte, long before a 3 MiB slot is erased.
HOST_TEST(ota_image_rejects_things_that_are_not_firmware_at_all) {
  std::vector<uint8_t> jpeg(ota::kImagePrefixBytes, 0);
  jpeg[0] = 0xFF;
  jpeg[1] = 0xD8;
  EXPECT_TRUE(ota::inspect_image_prefix(jpeg.data(), jpeg.size()).verdict ==
              ota::ImageVerdict::NotAnEspImage);

  // An HTML error page a proxy or captive portal substituted for the download.
  const char* html = "<!DOCTYPE html><html><head><title>404 Not Found</title>";
  std::vector<uint8_t> page(ota::kImagePrefixBytes, ' ');
  std::memcpy(page.data(), html, std::strlen(html));
  EXPECT_TRUE(ota::inspect_image_prefix(page.data(), page.size()).verdict ==
              ota::ImageVerdict::NotAnEspImage);
}

HOST_TEST(ota_image_rejects_wrong_chip_project_and_non_applications) {
  std::vector<uint8_t> other_chip = valid_prefix();
  other_chip[12] = 0x05;  // ESP32-C3
  EXPECT_TRUE(
      ota::inspect_image_prefix(other_chip.data(), other_chip.size()).verdict ==
      ota::ImageVerdict::WrongChip);

  // A bootloader or partition table: a real ESP image with no app descriptor.
  std::vector<uint8_t> not_app = valid_prefix();
  not_app[35] = 0x00;
  EXPECT_TRUE(
      ota::inspect_image_prefix(not_app.data(), not_app.size()).verdict ==
      ota::ImageVerdict::NotAnApplication);

  // Another ESP32-S3 project's firmware. Rejected, but it still reports what
  // it was - "wrong file" is actionable, an unexplained refusal is not.
  const std::vector<uint8_t> other = valid_prefix("some_other_project", "9.9.9");
  const ota::ImageInfo info =
      ota::inspect_image_prefix(other.data(), other.size());
  EXPECT_TRUE(info.verdict == ota::ImageVerdict::WrongProject);
  EXPECT_TRUE(info.project_name == "some_other_project");
  EXPECT_TRUE(info.version == "9.9.9");
}

HOST_TEST(ota_image_handles_short_and_unterminated_input_safely) {
  const std::vector<uint8_t> image = valid_prefix();
  // One byte short of a decision. Must say TooShort rather than read past the
  // buffer or, worse, accept on the fields it did manage to reach.
  EXPECT_TRUE(ota::inspect_image_prefix(image.data(), image.size() - 1)
                  .verdict == ota::ImageVerdict::TooShort);
  EXPECT_TRUE(ota::inspect_image_prefix(image.data(), 0).verdict ==
              ota::ImageVerdict::TooShort);
  EXPECT_TRUE(ota::inspect_image_prefix(nullptr, 999).verdict ==
              ota::ImageVerdict::TooShort);

  // A hostile image need not NUL-terminate its fixed-width fields; the read
  // must stop at the field boundary rather than running into the next one.
  std::vector<uint8_t> unterminated = valid_prefix();
  std::memset(unterminated.data() + 80, 'A', 32);
  const ota::ImageInfo info =
      ota::inspect_image_prefix(unterminated.data(), unterminated.size());
  EXPECT_EQ(static_cast<int>(info.project_name.size()), 32);
  EXPECT_TRUE(info.verdict == ota::ImageVerdict::WrongProject);
}

// Re-flashing the installed version is how you recover from a bad build with
// the only image you have on hand. A validator that blocks it is a validator
// that turns a recoverable board into a brick.
HOST_TEST(ota_image_does_not_gate_on_version) {
  for (const char* version : {"0.1.0", "0.0.1", "99.0.0", ""}) {
    const std::vector<uint8_t> image = valid_prefix("layout_carousel", version);
    EXPECT_TRUE(ota::inspect_image_prefix(image.data(), image.size())
                    .verdict == ota::ImageVerdict::Ok);
  }
}

HOST_TEST(ota_image_verdict_messages_are_ascii_and_distinct) {
  const ota::ImageVerdict verdicts[] = {
      ota::ImageVerdict::TooShort,         ota::ImageVerdict::NotAnEspImage,
      ota::ImageVerdict::WrongChip,        ota::ImageVerdict::NotAnApplication,
      ota::ImageVerdict::WrongProject};
  for (const ota::ImageVerdict verdict : verdicts) {
    const char* message = ota::image_verdict_message(verdict);
    EXPECT_TRUE(message[0] != '\0');
    for (const char* c = message; *c != '\0'; ++c) {
      EXPECT_TRUE(static_cast<unsigned char>(*c) < 0x80);
    }
    // Each rejection must say something different, or the message is noise.
    EXPECT_TRUE(std::strcmp(message, "OK") != 0);
  }
}

// The failure this guards against is silent: write the whole chunk after
// buffering part of it and the overlap lands in flash twice, shifting
// everything after it. The image still passes its header check and only fails
// much later, as a corrupt slot.
HOST_TEST(ota_prefix_reassembles_exactly_once_across_chunk_boundaries) {
  const std::vector<uint8_t> image = valid_prefix();
  // A payload longer than the prefix, with a recognisable tail.
  std::vector<uint8_t> full = image;
  for (int i = 0; i < 200; ++i) full.push_back(static_cast<uint8_t>(i));

  // Feed it in awkward pieces, none aligned to the 112-byte boundary, and
  // rebuild exactly what a Session would write to flash.
  for (const std::size_t chunk : {std::size_t{1}, std::size_t{7},
                                  std::size_t{50}, std::size_t{111},
                                  std::size_t{113}, std::size_t{5000}}) {
    ota::PrefixInspector inspector;
    std::vector<uint8_t> flashed;
    for (std::size_t offset = 0; offset < full.size(); offset += chunk) {
      const std::size_t length = std::min(chunk, full.size() - offset);
      const uint8_t* data = full.data() + offset;
      if (!inspector.ready()) {
        inspector.feed(data, length);
        if (!inspector.ready()) continue;
        flashed.insert(flashed.end(), inspector.buffer(),
                       inspector.buffer() + ota::kImagePrefixBytes);
        const std::size_t consumed = inspector.consumed_from_last_chunk();
        flashed.insert(flashed.end(), data + consumed, data + length);
        continue;
      }
      flashed.insert(flashed.end(), data, data + length);
    }
    EXPECT_TRUE(inspector.ready());
    EXPECT_TRUE(inspector.verdict() == ota::ImageVerdict::Ok);
    // Byte-for-byte identical to the input: nothing duplicated, nothing lost.
    EXPECT_EQ(static_cast<int>(flashed.size()), static_cast<int>(full.size()));
    EXPECT_TRUE(flashed == full);
  }
}

HOST_TEST(ota_prefix_latches_its_verdict_and_never_rejudges) {
  std::vector<uint8_t> jpeg(ota::kImagePrefixBytes, 0);
  jpeg[0] = 0xFF;
  ota::PrefixInspector inspector;
  inspector.feed(jpeg.data(), jpeg.size());
  EXPECT_TRUE(inspector.verdict() == ota::ImageVerdict::NotAnEspImage);

  // A later chunk that looks like a valid header must not launder the
  // rejection into an acceptance.
  const std::vector<uint8_t> image = valid_prefix();
  inspector.feed(image.data(), image.size());
  EXPECT_TRUE(inspector.verdict() == ota::ImageVerdict::NotAnEspImage);
  EXPECT_EQ(static_cast<int>(inspector.consumed_from_last_chunk()), 0);
}

HOST_TEST(ota_prefix_holds_an_upload_that_ends_inside_the_header) {
  const std::vector<uint8_t> image = valid_prefix();
  ota::PrefixInspector inspector;
  inspector.feed(image.data(), 40);
  // No verdict, so a Session built on this never opens the slot - which is
  // what stops a stalled upload from erasing the spare copy for nothing.
  EXPECT_TRUE(!inspector.ready());
  EXPECT_EQ(static_cast<int>(inspector.buffered()), 40);
}

// The image validator deliberately does not gate on version, so this function
// is the only thing preventing a periodic update check from reinstalling the
// same firmware forever.
HOST_TEST(ota_version_only_reports_newer_for_a_genuine_increase) {
  EXPECT_TRUE(ota::is_newer("0.2.0", "0.1.0"));
  EXPECT_TRUE(ota::is_newer("1.0.0", "0.9.9"));
  EXPECT_TRUE(ota::is_newer("0.1.1", "0.1.0"));
  // A GitHub tag_name carries a leading v; the app descriptor does not.
  EXPECT_TRUE(ota::is_newer("v0.2.0", "0.1.0"));
  EXPECT_TRUE(!ota::is_newer("v0.1.0", "0.1.0"));

  // Equal must be false, or every check installs again.
  EXPECT_TRUE(!ota::is_newer("0.1.0", "0.1.0"));
  EXPECT_TRUE(!ota::is_newer("0.1.0", "v0.1.0"));
  // Missing components are zero, so these are the same version.
  EXPECT_TRUE(!ota::is_newer("0.1", "0.1.0"));
  EXPECT_TRUE(!ota::is_newer("1", "1.0.0"));

  // Older must be false, including a downgrade offered by a mis-tagged release.
  EXPECT_TRUE(!ota::is_newer("0.1.0", "0.2.0"));
  EXPECT_TRUE(!ota::is_newer("0.9.9", "1.0.0"));

  // Numeric, not lexicographic: "0.10.0" is newer than "0.9.0" even though it
  // sorts earlier as a string.
  EXPECT_TRUE(ota::is_newer("0.10.0", "0.9.0"));
  EXPECT_TRUE(!ota::is_newer("0.9.0", "0.10.0"));
}

HOST_TEST(ota_version_refuses_to_act_on_anything_it_cannot_parse) {
  // A garbage tag parses to 0.0.0, which would read as "newer than nothing".
  // Both sides must contain digits before any comparison is trusted.
  for (const char* junk : {"", "v", "latest", "nightly", "release"}) {
    EXPECT_TRUE(!ota::is_newer(junk, "0.1.0"));
    EXPECT_TRUE(!ota::is_newer("0.2.0", junk));
  }
  // A suffix after the numbers is fine and must not corrupt the comparison.
  EXPECT_TRUE(ota::is_newer("0.2.0-rc1", "0.1.0"));
  EXPECT_TRUE(!ota::is_newer("0.1.0-rc1", "0.1.0"));
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

HOST_TEST(release_notes_pass_through_plain_ascii_unchanged) {
  EXPECT_TRUE(ota::sanitize_release_notes("Fixed the settings menu.", 80) ==
              "Fixed the settings menu.");
  // Empty input is not an error case to report; it is simply nothing to show.
  EXPECT_TRUE(ota::sanitize_release_notes("", 80).empty());
}

HOST_TEST(release_notes_collapse_markdown_whitespace_to_single_spaces) {
  // Blank lines between paragraphs, leading/trailing whitespace, and a
  // Windows line ending all collapse the same way: this row has space for a
  // short excerpt, not the release body's own formatting.
  EXPECT_TRUE(ota::sanitize_release_notes(
                  "  Fixed a bug.\r\n\r\n- Also improved startup.\n", 80) ==
              "Fixed a bug. - Also improved startup.");
}

HOST_TEST(release_notes_with_any_non_ascii_byte_are_withheld_entirely) {
  // A real CJK character: this project's CJK font is a fixed, curated
  // 121-glyph subset lifted from its own UI strings (check-cjk-font.py), not
  // a general-purpose font, so a release-body character has no guaranteed
  // glyph even when it happens to be Chinese - and it is not in the
  // typography table, so nothing normalises it away.
  EXPECT_TRUE(ota::sanitize_release_notes("Fixed \xe4\xbf\xae\xe5\xbe\xa9.", 80)
                  .empty());
  // An emoji is exactly as unmapped, and exactly as untrustworthy.
  EXPECT_TRUE(ota::sanitize_release_notes("Shipped it \xf0\x9f\x9a\x80", 80)
                  .empty());
  // A body that is entirely non-ASCII must not fall back to whatever Latin
  // punctuation happened to survive - it must produce nothing, same as the
  // mixed case above.
  EXPECT_TRUE(ota::sanitize_release_notes("\xe4\xbf\xae\xe5\xbe\xa9\xe5\xa5\xbd",
                                          80)
                  .empty());
}

HOST_TEST(release_notes_diagnostic_reports_the_byte_and_offset_when_withheld) {
  const ota::ReleaseNotesResult result =
      ota::sanitize_release_notes_diagnostic(
          "Fixed \xe4\xbf\xae\xe5\xbe\xa9.", 80);
  EXPECT_TRUE(result.text.empty());
  EXPECT_TRUE(result.withheld_non_ascii);
  // "Fixed " is 6 ASCII bytes; the first byte of the CJK character sits at
  // offset 6 in the normalised text (unchanged here, since none of it
  // matches the typography table).
  EXPECT_EQ(static_cast<int>(result.offending_offset), 6);
  EXPECT_EQ(static_cast<int>(result.offending_byte), 0xe4);

  // Genuinely empty input is not a mistake to report - the distinction this
  // struct exists for.
  const ota::ReleaseNotesResult empty_result =
      ota::sanitize_release_notes_diagnostic("", 80);
  EXPECT_TRUE(empty_result.text.empty());
  EXPECT_TRUE(!empty_result.withheld_non_ascii);
}

HOST_TEST(release_notes_normalise_typographic_punctuation_before_the_gate) {
  // The exact case that motivated this: GitHub's editor turns a plain
  // apostrophe into a curly one on its own, and withholding the whole body
  // over a keystroke the author never chose to make would be a worse
  // failure than the one the gate exists to prevent.
  EXPECT_TRUE(ota::sanitize_release_notes("Fixed a bug that doesn\xe2\x80\x99t "
                                          "matter.",
                                          80) ==
              "Fixed a bug that doesn't matter.");
  // Curly double quotes.
  EXPECT_TRUE(ota::sanitize_release_notes(
                  "Renamed \xe2\x80\x9csettings\xe2\x80\x9d.", 80) ==
              "Renamed \"settings\".");
  // En dash and em dash both become a plain hyphen.
  EXPECT_TRUE(ota::sanitize_release_notes("Pages 1\xe2\x80\x93" "2.", 80) ==
              "Pages 1-2.");
  EXPECT_TRUE(ota::sanitize_release_notes("Faster \xe2\x80\x94 a lot.", 80) ==
              "Faster - a lot.");
  // Horizontal ellipsis becomes three literal dots, matching this project's
  // own truncation output rather than clashing with it.
  EXPECT_TRUE(ota::sanitize_release_notes("Still working on it\xe2\x80\xa6",
                                          80) == "Still working on it...");
  // Bullet becomes a hyphen - this project's own list-marker convention
  // (see the whitespace-collapse test above, which already writes "- ").
  EXPECT_TRUE(ota::sanitize_release_notes("\xe2\x80\xa2 Fixed a crash.", 80) ==
              "- Fixed a crash.");
  // Non-breaking space becomes a plain space, then collapses like any other.
  EXPECT_TRUE(ota::sanitize_release_notes("No\xc2\xa0" "crash.", 80) ==
              "No crash.");
  // Rightwards arrow becomes "->", the same substitution this project's own
  // UI code makes for the identical font reason (render_ota.cpp).
  EXPECT_TRUE(ota::sanitize_release_notes("0.1.2 \xe2\x86\x92 0.1.4.", 80) ==
              "0.1.2 -> 0.1.4.");
}

HOST_TEST(release_notes_longer_than_the_budget_ellipsise_on_a_word_boundary) {
  const std::string result = ota::sanitize_release_notes(
      "This release fixes the settings menu focus bug and adds AirPlay "
      "support for the speaker.",
      40);
  // Within budget including the ellipsis, and the ellipsis is three literal
  // ASCII dots - never U+2026, which is exactly the kind of character this
  // function exists to keep off the panel.
  EXPECT_TRUE(result.size() <= 40);
  EXPECT_TRUE(result.substr(result.size() - 3) == "...");
  // Cut on a word boundary, not mid-word: the text up to the cut must be a
  // prefix of the original, not a fragment of a word that continues past it.
  const std::string body_before_ellipsis = result.substr(0, result.size() - 3);
  EXPECT_TRUE(
      body_before_ellipsis ==
      "This release fixes the settings menu");
}

HOST_TEST(release_notes_shorter_than_the_budget_need_no_ellipsis) {
  const std::string result =
      ota::sanitize_release_notes("Short fix.", 80);
  EXPECT_TRUE(result == "Short fix.");
}
