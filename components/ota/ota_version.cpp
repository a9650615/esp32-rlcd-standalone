#include "ota_version.hpp"

#include <cctype>

namespace ota {
namespace {

// Reads the next dotted component starting at `index`, advancing past it and
// any single separator. Stops at the first non-digit, so a "-rc1" or "+build"
// suffix simply ends the numeric part rather than corrupting it.
//
// `saw_digit` reports whether a number was actually present, which is how a
// string with no digits at all is told apart from a genuine leading zero.
long next_component(const std::string& text, std::size_t& index,
                    bool& saw_digit) {
  saw_digit = false;
  long value = 0;
  while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
    saw_digit = true;
    // Clamp rather than overflow: a version field long enough to wrap is
    // malformed, and wrapping could make it compare as older than it is.
    if (value < 1'000'000) value = value * 10 + (text[index] - '0');
    ++index;
  }
  if (index < text.size() && text[index] == '.') ++index;
  return value;
}

std::size_t start_of_digits(const std::string& text) {
  // Skips a leading "v"/"V" and any surrounding space, so a GitHub tag_name
  // ("v0.2.0") compares against an app descriptor version ("0.2.0").
  std::size_t index = 0;
  while (index < text.size() &&
         std::isspace(static_cast<unsigned char>(text[index]))) {
    ++index;
  }
  if (index < text.size() && (text[index] == 'v' || text[index] == 'V')) ++index;
  return index;
}

}  // namespace

int compare_versions(const std::string& a, const std::string& b) {
  std::size_t ia = start_of_digits(a);
  std::size_t ib = start_of_digits(b);
  // Three passes covers major.minor.patch; a fourth component would need this
  // raised, but nothing in this project emits one.
  for (int component = 0; component < 3; ++component) {
    bool digit_a = false;
    bool digit_b = false;
    const long va = next_component(a, ia, digit_a);
    const long vb = next_component(b, ib, digit_b);
    if (va != vb) return va < vb ? -1 : 1;
  }
  return 0;
}

bool is_newer(const std::string& candidate, const std::string& running) {
  // A version with no digits at all parses as 0.0.0, which would read as
  // "newer" against nothing and equal against 0.0.0. Reject both inputs
  // outright instead of letting a garbage tag drive an install.
  const std::size_t ca = start_of_digits(candidate);
  const std::size_t ra = start_of_digits(running);
  if (ca >= candidate.size() ||
      !std::isdigit(static_cast<unsigned char>(candidate[ca]))) {
    return false;
  }
  if (ra >= running.size() ||
      !std::isdigit(static_cast<unsigned char>(running[ra]))) {
    return false;
  }
  return compare_versions(candidate, running) > 0;
}

}  // namespace ota
