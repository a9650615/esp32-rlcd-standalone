#define UI_THEME_GEOMETRY_ONLY
#include "app_snapshot.hpp"
#include "card_contract.hpp"

#include "test_support.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

// The card wire contract, emitted from the C++ the device enforces so a host
// written in another language does not keep a second copy of these numbers.
//
// A rejected card is silent: the device drops it, the slot reports itself
// unavailable, and the host still logs its own 200. Drift here therefore looks
// like a card that never appears, with nothing anywhere saying why - which is
// the whole reason the budgets are published rather than left for a host
// author to infer from a rendered example.

namespace {

std::string quoted(const std::string& value) { return "\"" + value + "\""; }

std::string field(const std::string& key, long long value) {
  return "    " + quoted(key) + ": " + std::to_string(value);
}

std::string field(const std::string& key, const std::string& value) {
  return "    " + quoted(key) + ": " + quoted(value);
}

// FNV-1a. Not a security property - it exists so a host holding cards drawn
// for last month's budgets throws them away instead of serving a card this
// device will silently refuse.
std::string schema_hash(const std::string& body) {
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char byte : body) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  static const char* digits = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[i] = digits[hash & 0xF];
    hash >>= 4;
  }
  return out;
}

std::string emit_schema() {
  std::ostringstream body;
  body << "  " << quoted("_comment")
       << ": "
       << quoted("Every number here is emitted from the C++ this device "
                 "enforces (components/ui/include/card_contract.hpp), so a "
                 "host keeps no second copy that can drift. The device sends "
                 "schema_hash with its request; a host holding cards drawn "
                 "for a different hash should redraw them rather than serve "
                 "a card this device will silently refuse.")
       << ",\n";
  body << "  " << quoted("device") << ": " << quoted("rlcd-4.2") << ",\n";
  body << "  " << quoted("body_mask") << ": {\n"
       << field("width_px", ui::kCardBodyWidth) << ",\n"
       << field("height_px", ui::kCardBodyHeight) << ",\n"
       << field("stride_bytes", ui::kCardBodyStride) << ",\n"
       << field("max_bytes", ui::kCardBodyMaxBytes) << ",\n"
       << field("format",
                "1bpp rows padded to stride_bytes, MSB first, set bit = ink")
       << ",\n"
       << field("encoding", "base64") << ",\n"
       << field("_comment",
                "A smaller mask is legal and is centred in the body area; a "
                "larger one is rejected. The body is the whole content area - "
                "the device draws only the system tray and the page dots, so "
                "the card's own title belongs in here.")
       << "\n  },\n";
  body << "  " << quoted("cards") << ": {\n"
       << field("max_items", ui::kMaxCards) << ",\n"
       << field("_comment",
                "Extra cards are dropped, not rotated in. The bound is "
                "attention, not memory.")
       << "\n  },\n";
  body << "  " << quoted("label") << ": {\n"
       << field("max_bytes", ui::kMaxCardLabelLength) << ",\n"
       << field("charset", "ASCII 32-126") << ",\n"
       << field("_comment",
                "Never drawn. It names the card in the device log and nowhere "
                "else; anything a reader should see belongs in body_mask.")
       << "\n  },\n";
  body << "  " << quoted("dwell_seconds") << ": {\n"
       << field("min", ui::kMinCardDwellSeconds) << ",\n"
       << field("max", ui::kMaxCardDwellSeconds) << ",\n"
       << field("default", 12) << "\n  },\n";
  body << "  " << quoted("priority") << ": {\n"
       << "    " << quoted("values")
       << ": [\"background\", \"normal\", \"elevated\", \"urgent\"],\n"
       << field("_comment",
                "Carried end to end and currently consulted by nothing: the "
                "device orders its carousel by registration order alone. Send "
                "the honest value now so the policy that reads it later is "
                "written against real cards.")
       << "\n  },\n";
  body << "  " << quoted("document") << ": {\n"
       << field("max_bytes", ui::kMaxDocumentBytes) << ",\n"
       << field("_comment",
                "The serialised response, whole. A longer body is discarded "
                "before the parser sees a byte of it, so the panel keeps what "
                "it had while the host logs its own 200.")
       << "\n  }";

  const std::string inner = body.str();
  std::ostringstream document;
  document << "{\n"
           << "  " << quoted("_generated_by")
           << ": " << quoted("tests/host/test_card_contract.cpp - do not edit "
                             "by hand")
           << ",\n"
           << "  " << quoted("schema_hash") << ": " << quoted(schema_hash(inner))
           << ",\n"
           << inner << "\n}\n";
  return document.str();
}

std::string golden_path() { return std::string(GOLDEN_DIR) + "/card-schema.json"; }

std::string read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return {};
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

}  // namespace

HOST_TEST(card_schema_matches_the_checked_in_golden) {
  const std::string emitted = emit_schema();
  const std::string golden = read_file(golden_path());
  if (emitted != golden) {
    const std::string actual = golden_path() + ".actual";
    std::ofstream(actual, std::ios::binary) << emitted;
    throw std::runtime_error(
        "card schema drifted from " + golden_path() +
        "; the emitted version was written to " + actual +
        " - review the difference, then copy it over the golden");
  }
}

HOST_TEST(card_body_is_the_content_area_every_rotation_page_gets) {
  // Derived, never transcribed: if the tray or the dot band changes height,
  // this moves with it and the golden above fails until the host is told.
  const ui::Rect body = ui::card_body_bounds();
  EXPECT_EQ(body.width, ui::kCardBodyWidth);
  EXPECT_EQ(body.height, ui::kCardBodyHeight);
  EXPECT_TRUE(ui::within_safe_canvas(body));
  EXPECT_EQ(ui::card_body_bounds().width,
            ui::content_bounds(ui::safe_canvas(),
                               app_core::PageId::Weather).width);
  EXPECT_EQ(ui::card_body_bounds().height,
            ui::content_bounds(ui::safe_canvas(),
                               app_core::PageId::Weather).height);
}

HOST_TEST(card_mask_stride_pads_rows_to_whole_bytes) {
  EXPECT_EQ(ui::card_mask_stride(1), 1);
  EXPECT_EQ(ui::card_mask_stride(8), 1);
  EXPECT_EQ(ui::card_mask_stride(9), 2);
  EXPECT_EQ(ui::kCardBodyMaxBytes,
            ui::kCardBodyStride * ui::kCardBodyHeight);
}

HOST_TEST(document_bound_covers_a_full_response_of_largest_cards) {
  // base64 is four out per three in, so the bound has to be computed from the
  // encoded size and not the raw one. A bound that is too small is a response
  // discarded whole - every card lost, not the last one.
  const int encoded = ui::base64_length(ui::kCardBodyMaxBytes);
  EXPECT_TRUE(encoded >= ui::kCardBodyMaxBytes * 4 / 3);
  EXPECT_TRUE(ui::kMaxDocumentBytes > ui::kMaxCards * encoded);
}

HOST_TEST(card_is_drawable_rejects_what_the_panel_cannot_draw) {
  static const uint8_t bits[ui::kCardBodyMaxBytes] = {};
  ui::Card card;
  card.body = {bits, ui::kCardBodyWidth, ui::kCardBodyHeight};
  card.dwell_seconds = 12;
  EXPECT_TRUE(ui::card_is_drawable(card));

  // A smaller mask is legal - it gets centred.
  ui::Card small = card;
  small.body.width = 10;
  small.body.height = 10;
  EXPECT_TRUE(ui::card_is_drawable(small));

  // Everything below would draw outside the body area, or not at all.
  ui::Card no_bits = card;
  no_bits.body.bits = nullptr;
  EXPECT_TRUE(!ui::card_is_drawable(no_bits));

  ui::Card too_wide = card;
  too_wide.body.width = ui::kCardBodyWidth + 1;
  EXPECT_TRUE(!ui::card_is_drawable(too_wide));

  ui::Card too_tall = card;
  too_tall.body.height = ui::kCardBodyHeight + 1;
  EXPECT_TRUE(!ui::card_is_drawable(too_tall));

  ui::Card empty = card;
  empty.body.width = 0;
  EXPECT_TRUE(!ui::card_is_drawable(empty));

  // A dwell nobody would sit through is a stuck panel, not a long card.
  ui::Card forever = card;
  forever.dwell_seconds = ui::kMaxCardDwellSeconds + 1;
  EXPECT_TRUE(!ui::card_is_drawable(forever));

  ui::Card blink = card;
  blink.dwell_seconds = ui::kMinCardDwellSeconds - 1;
  EXPECT_TRUE(!ui::card_is_drawable(blink));
}
