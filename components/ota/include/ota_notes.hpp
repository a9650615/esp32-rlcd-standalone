#pragma once

#include <cstddef>
#include <string>

namespace ota {

// Turns a GitHub release's free-text `body` into something safe to put on
// this panel, or an empty string when it is not. Header-only, like
// ota_decision.hpp: small enough that a .cpp would only add a file to find.
//
// Why free text is trustworthy here at all, when a LAN push's claimed
// version never is (see app_core::OtaData::version's own comment): this
// text only ever reaches here after ota_release.cpp's TLS fetch of
// api.github.com, checked against the certificate bundle. TLS proves the
// bytes came from GitHub; GitHub proves the repository owner published
// them. A LAN push has no such chain - whoever is pushing chose that text -
// which is exactly why OtaData::version is read out of the image's own
// descriptor instead of believed from the sender. The same reasoning is why
// nothing in this file is ever reachable from that path: app_core::OtaData,
// which is all a LAN push's confirm prompt (ota_confirm.cpp, driven from
// components/wifi_provision/portal.cpp) ever gets to populate, has no notes
// field to carry one. That omission is deliberate, not an oversight to fill
// in later - see app_snapshot.hpp's own comment on OtaData.
//
// ASCII only, not "whatever this app's UI already knows how to draw": the
// CJK glyphs this panel can render are a fixed, curated 121-character
// subset lifted from this app's own compiled-in strings (see
// scripts/check-cjk-font.py, which checks exactly that set against
// components/ui/ui_strings.cpp at build time - it has no opinion on text
// that never passes through ui_strings.cpp at all). A release body is free
// text written months from now, so nothing about that subset - or about
// Montserrat's own glyph range - is guaranteed to cover it.
//
// Confirmed rather than assumed, by reading LVGL's own fallback path
// (lv_font.c's lv_font_get_glyph_dsc, lv_draw_label.c's draw_letter_cb,
// lv_draw_sw_letter.c's draw_letter_cb): with
// CONFIG_LV_USE_FONT_PLACEHOLDER=y (this project's setting), a codepoint
// missing from every font in the fallback chain does not draw blank - it
// draws a visible bordered box, one per missing character. That rules out
// "render only the characters that exist": for a body that is mostly a
// non-Latin script, or that uses a smart quote or an em dash Montserrat's
// own glyph range was never checked against, the result would be a sentence
// with words silently missing and boxes left in their place, which is worse
// than showing nothing. So this takes the plainest option instead: if the
// body is not already plain ASCII once whitespace is collapsed, there is
// nothing trustworthy to show, and saying nothing is the honest rendering -
// the same principle the LAN-push path applies above, for a different
// reason.
//
// A signature over the body would let a LAN push earn this same trust
// later - the operator's own follow-on, not built here. It would have to be
// an application-level check: this project does not burn eFuses, so
// hardware-rooted secure boot is off the table, and an app-level signature
// check has weaker guarantees than one the bootloader itself enforces.
// Getting that trade-off right deserves its own pass, not a rider on this
// one.
//
// Truncates to `max_chars`, cutting on the nearest earlier word boundary
// where there is one, with a literal "..." (never U+2026 - that is exactly
// the kind of character this function exists to keep off the panel).
// `max_chars` is a plain character count, not a measured pixel width - an
// approximation, not the exact-fit measurement this project uses for its
// own compiled-in strings, because ota_release.cpp (the only caller) is a
// network component with no LVGL linked in at all, so it has no font
// metrics to measure against even if it wanted to.
inline std::string sanitize_release_notes(const std::string& raw,
                                          std::size_t max_chars) {
  // Collapse all whitespace - including markdown's blank lines between
  // paragraphs - to single spaces. The row this feeds has room for a short
  // excerpt, not a formatted changelog, so preserving line breaks would
  // just spend the budget on blank space instead of words.
  std::string collapsed;
  collapsed.reserve(raw.size());
  bool last_was_space = true;  // Trims leading whitespace for free.
  for (unsigned char c : raw) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if (!last_was_space) collapsed.push_back(' ');
      last_was_space = true;
      continue;
    }
    // One non-ASCII byte voids the whole excerpt rather than dropping just
    // that character - see the file comment above for why.
    if (c < 0x20 || c > 0x7e) return {};
    collapsed.push_back(static_cast<char>(c));
    last_was_space = false;
  }
  if (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
  if (collapsed.empty()) return {};
  if (collapsed.size() <= max_chars) return collapsed;

  constexpr std::size_t kEllipsisLen = 3;  // "..."
  const std::size_t budget = max_chars > kEllipsisLen ? max_chars - kEllipsisLen : 0;
  std::size_t cut = collapsed.rfind(' ', budget);
  if (cut == std::string::npos || cut == 0) cut = budget;
  std::string truncated = collapsed.substr(0, cut);
  while (!truncated.empty() && truncated.back() == ' ') truncated.pop_back();
  return truncated + "...";
}

}  // namespace ota
