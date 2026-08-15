#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace wifi_config {

struct Credentials {
  std::string ssid;
  std::string password;
};

enum class CredentialError {
  None,
  SsidEmpty,
  SsidTooLong,
  PasswordTooShort,
  PasswordTooLong
};

// SSID must be 1..32 bytes. Password is empty (open network) or 8..63 bytes
// (WPA2-PSK). Lengths are byte lengths, not character counts.
CredentialError validate(const Credentials& credentials);

// Decodes application/x-www-form-urlencoded text: '+' becomes a space and
// '%XX' becomes the byte XX (hex, either case). A malformed or truncated '%'
// escape is kept literally rather than dropped.
std::string url_decode(std::string_view text);

// Parses an application/x-www-form-urlencoded body for the "ssid" and
// "password" keys; unknown keys are ignored. A missing "password" key means
// an empty password. Returns false only if the "ssid" key is absent.
bool parse_form(std::string_view body, Credentials& out);

// Extracts and url-decodes the value for `key` from an
// application/x-www-form-urlencoded string - a POST body or a URL query
// string (without the leading '?'), same syntax either way. Returns false
// and leaves `out` untouched if `key` is absent.
bool find_form_value(std::string_view body, std::string_view key, std::string& out);

// Builds the setup-mode access point SSID, e.g. "RLCD-A1B2C3", from the last
// three bytes of the given MAC address.
std::string setup_ap_ssid(const std::array<uint8_t, 6>& mac);

// Builds the setup-portal QR payload: the bare portal URL with the
// per-session page password appended as a query parameter, e.g.
// "http://192.168.4.1/?pw=aB3dEfGh". Returns portal_url unchanged if
// password is empty. The password alphabet (format_passphrase) is
// alnum-only and therefore already URL-safe; that is asserted rather than
// implemented as a percent-encoder nothing exercises.
std::string portal_qr_payload(std::string_view portal_url, std::string_view password);

// Length of a generated setup-portal page password.
inline constexpr std::size_t kPassphraseLength = 8;

// Formats an 8-character setup-portal page password from caller-supplied
// random bytes (one byte consumed per character), drawn from an alphabet
// that excludes 0/O/1/l/I so it can be retyped from the screen. Returns an
// empty string if fewer than kPassphraseLength bytes are supplied.
std::string format_passphrase(const uint8_t* bytes, std::size_t length);

// Constant-time comparison for the portal page password: never early-returns
// on the first differing byte, so a network attacker cannot time their way
// to the password one character at a time.
bool constant_time_equal(std::string_view a, std::string_view b);

// Provisioning lifecycle. Owns no timers or threads; callers drive it with
// events observed elsewhere.
class StateMachine {
 public:
  enum class State { Connecting, Connected, SetupAp };
  static constexpr int kMaxRetries = 5;

  explicit StateMachine(bool has_saved_credentials);

  State state() const { return state_; }
  int retries() const { return retries_; }

  void on_connected();
  void on_disconnected();
  void on_setup_gesture();
  void on_credentials_saved();

 private:
  State state_;
  bool has_credentials_;
  int retries_ = 0;
};

}  // namespace wifi_config
