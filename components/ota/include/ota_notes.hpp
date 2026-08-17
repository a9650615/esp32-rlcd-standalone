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
// The operator has decided release notes will be written in English, which
// settles that this gate's shape is right - no Chinese fallback string is
// needed. It does not settle "is pure ASCII", though: GitHub's own editor
// (and most editors) turns a plain apostrophe into a curly one, an em dash
// into U+2014, "..." into U+2026, on their own, without the author ever
// choosing to type a non-ASCII character. Withholding the whole body over a
// keystroke nobody made on purpose is a worse failure than the one this
// gate exists to prevent, so the handful of well-known offenders below are
// normalised to the ASCII character they visually stand in for *before* the
// gate runs.
//
// This is substitution, not the partial rendering rejected above: every
// mapping here replaces one character with the ASCII punctuation mark it is
// a stylistic variant of, in every position, and changes no words. It is
// not a step toward transliteration - nothing here or ever added to this
// table may spell out a word (a ligature, an accented letter standing in
// for a plain one, "vs" for a symbol that means something else). Anything
// still non-ASCII after this table is exactly as untrustworthy as before and
// is still withheld whole, with a diagnostic logged - see
// sanitize_release_notes_diagnostic() below - rather than silently.
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

// One typographic character GitHub's editor is known to substitute for a
// plain keystroke, and the single ASCII character it stands in for. `utf8`
// is the exact byte sequence to match (not a codepoint - this file does no
// general UTF-8 decoding, on purpose: matching a short, fixed list of known
// byte sequences literally is enough for this and needs no decoder).
struct TypographyMapping {
  const char* utf8;
  char ascii;
  // Two-character replacements ("->", "...") use this instead of `ascii`;
  // `ascii` is 0 when this is set.
  const char* ascii_run;
};

// This table is deliberately short and deliberately punctuation-only - see
// the file comment above for why it must stay that way. Order does not
// matter: each entry is matched by its own distinct byte sequence.
inline constexpr TypographyMapping kTypographyMappings[] = {
    {"\xe2\x80\x98", '\'', nullptr},  // U+2018 LEFT SINGLE QUOTATION MARK
    {"\xe2\x80\x99", '\'', nullptr},  // U+2019 RIGHT SINGLE QUOTATION MARK (the common curly apostrophe)
    {"\xe2\x80\x9c", '"', nullptr},   // U+201C LEFT DOUBLE QUOTATION MARK
    {"\xe2\x80\x9d", '"', nullptr},   // U+201D RIGHT DOUBLE QUOTATION MARK
    {"\xe2\x80\x93", '-', nullptr},   // U+2013 EN DASH
    {"\xe2\x80\x94", '-', nullptr},   // U+2014 EM DASH
    {"\xe2\x80\xa2", '-', nullptr},   // U+2022 BULLET
    {"\xc2\xa0", ' ', nullptr},       // U+00A0 NO-BREAK SPACE
    {"\xe2\x80\xa6", 0, "..."},       // U+2026 HORIZONTAL ELLIPSIS
    {"\xe2\x86\x92", 0, "->"},        // U+2192 RIGHTWARDS ARROW - this project
                                      // writes "->" rather than the glyph
                                      // itself elsewhere too (render_ota.cpp),
                                      // for the same font reason.
};

inline std::string normalize_typography(const std::string& raw) {
  std::string out = raw;
  for (const TypographyMapping& mapping : kTypographyMappings) {
    const std::string needle = mapping.utf8;
    const std::string replacement =
        mapping.ascii_run != nullptr ? mapping.ascii_run
                                     : std::string(1, mapping.ascii);
    std::size_t pos = 0;
    while ((pos = out.find(needle, pos)) != std::string::npos) {
      out.replace(pos, needle.size(), replacement);
      pos += replacement.size();
    }
  }
  return out;
}

// What sanitize_release_notes() below could not tell a log line without
// pulling in ESP-IDF logging, which this header cannot depend on and still
// stay host-testable (see ota_release.cpp, the only place that logs it).
struct ReleaseNotesResult {
  // The sanitized, truncated excerpt - empty when there was nothing to show,
  // for either reason below.
  std::string text;
  // True only when there was a non-empty body and normalization still left
  // a byte outside 0x20-0x7E - i.e. this was a real, fixable authoring
  // mistake, not simply "no notes". Distinguishing the two is the entire
  // point of this struct.
  bool withheld_non_ascii = false;
  // The first offending byte, and its offset into the normalized text - not
  // the raw GitHub body's own byte offset, since typography normalization
  // can shift positions, but close enough to say "roughly where" without
  // logging the body itself.
  unsigned char offending_byte = 0;
  std::size_t offending_offset = 0;
};

inline ReleaseNotesResult sanitize_release_notes_diagnostic(
    const std::string& raw, std::size_t max_chars) {
  ReleaseNotesResult result;
  const std::string normalized = normalize_typography(raw);

  // Collapse all whitespace - including markdown's blank lines between
  // paragraphs - to single spaces. The row this feeds has room for a short
  // excerpt, not a formatted changelog, so preserving line breaks would
  // just spend the budget on blank space instead of words.
  std::string collapsed;
  collapsed.reserve(normalized.size());
  bool last_was_space = true;  // Trims leading whitespace for free.
  for (std::size_t i = 0; i < normalized.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(normalized[i]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if (!last_was_space) collapsed.push_back(' ');
      last_was_space = true;
      continue;
    }
    // One byte outside plain ASCII voids the whole excerpt rather than
    // dropping just that character - see the file comment above for why.
    // Reported, not just refused: this is the one case that is a fixable
    // authoring mistake rather than "there is genuinely nothing to show".
    if (c < 0x20 || c > 0x7e) {
      result.withheld_non_ascii = true;
      result.offending_byte = c;
      result.offending_offset = i;
      return result;
    }
    collapsed.push_back(static_cast<char>(c));
    last_was_space = false;
  }
  if (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
  if (collapsed.empty()) return result;  // Genuinely no body - not a mistake.

  if (collapsed.size() <= max_chars) {
    result.text = collapsed;
    return result;
  }

  constexpr std::size_t kEllipsisLen = 3;  // "..."
  const std::size_t budget = max_chars > kEllipsisLen ? max_chars - kEllipsisLen : 0;
  std::size_t cut = collapsed.rfind(' ', budget);
  if (cut == std::string::npos || cut == 0) cut = budget;
  std::string truncated = collapsed.substr(0, cut);
  while (!truncated.empty() && truncated.back() == ' ') truncated.pop_back();
  result.text = truncated + "...";
  return result;
}

// Convenience wrapper for callers - most tests among them - that only need
// the text and not the reason an empty result came back.
inline std::string sanitize_release_notes(const std::string& raw,
                                          std::size_t max_chars) {
  return sanitize_release_notes_diagnostic(raw, max_chars).text;
}

}  // namespace ota
