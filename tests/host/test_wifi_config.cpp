#include "test_support.hpp"

#include "wifi_config.hpp"

#include <array>
#include <string>

using wifi_config::Credentials;
using wifi_config::CredentialError;
using wifi_config::StateMachine;

HOST_TEST(validate_rejects_empty_ssid) {
  EXPECT_EQ(wifi_config::validate(Credentials{"", ""}), CredentialError::SsidEmpty);
}

HOST_TEST(validate_accepts_thirty_two_byte_ssid) {
  const Credentials credentials{std::string(32, 'a'), ""};
  EXPECT_EQ(wifi_config::validate(credentials), CredentialError::None);
}

HOST_TEST(validate_rejects_thirty_three_byte_ssid) {
  const Credentials credentials{std::string(33, 'a'), ""};
  EXPECT_EQ(wifi_config::validate(credentials), CredentialError::SsidTooLong);
}

HOST_TEST(validate_accepts_empty_password_for_open_network) {
  EXPECT_EQ(wifi_config::validate(Credentials{"net", ""}), CredentialError::None);
}

HOST_TEST(validate_rejects_seven_byte_password) {
  const Credentials credentials{"net", std::string(7, 'p')};
  EXPECT_EQ(wifi_config::validate(credentials), CredentialError::PasswordTooShort);
}

HOST_TEST(validate_accepts_eight_byte_password) {
  const Credentials credentials{"net", std::string(8, 'p')};
  EXPECT_EQ(wifi_config::validate(credentials), CredentialError::None);
}

HOST_TEST(validate_accepts_sixty_three_byte_password) {
  const Credentials credentials{"net", std::string(63, 'p')};
  EXPECT_EQ(wifi_config::validate(credentials), CredentialError::None);
}

HOST_TEST(validate_rejects_sixty_four_byte_password) {
  const Credentials credentials{"net", std::string(64, 'p')};
  EXPECT_EQ(wifi_config::validate(credentials), CredentialError::PasswordTooLong);
}

HOST_TEST(url_decode_turns_plus_into_space) {
  EXPECT_EQ(wifi_config::url_decode("My+Net"), std::string("My Net"));
}

HOST_TEST(url_decode_handles_upper_and_lower_hex) {
  EXPECT_EQ(wifi_config::url_decode("hunter2%21"), std::string("hunter2!"));
  EXPECT_EQ(wifi_config::url_decode("%2b%2B"), std::string("++"));
}

HOST_TEST(url_decode_keeps_malformed_escape_literal) {
  EXPECT_EQ(wifi_config::url_decode("100%"), std::string("100%"));
  EXPECT_EQ(wifi_config::url_decode("100%2"), std::string("100%2"));
  EXPECT_EQ(wifi_config::url_decode("100%zz"), std::string("100%zz"));
}

HOST_TEST(parse_form_decodes_ssid_and_password) {
  Credentials credentials;
  const bool ok =
      wifi_config::parse_form("ssid=My+Net&password=hunter2%21", credentials);
  EXPECT_TRUE(ok);
  EXPECT_EQ(credentials.ssid, std::string("My Net"));
  EXPECT_EQ(credentials.password, std::string("hunter2!"));
}

HOST_TEST(parse_form_defaults_password_when_key_missing) {
  Credentials credentials;
  const bool ok = wifi_config::parse_form("ssid=OpenNet", credentials);
  EXPECT_TRUE(ok);
  EXPECT_EQ(credentials.ssid, std::string("OpenNet"));
  EXPECT_EQ(credentials.password, std::string(""));
}

HOST_TEST(parse_form_ignores_unknown_keys) {
  Credentials credentials;
  const bool ok =
      wifi_config::parse_form("foo=bar&ssid=Net&password=secret12", credentials);
  EXPECT_TRUE(ok);
  EXPECT_EQ(credentials.ssid, std::string("Net"));
  EXPECT_EQ(credentials.password, std::string("secret12"));
}

HOST_TEST(parse_form_fails_when_ssid_key_is_absent) {
  Credentials credentials;
  const bool ok = wifi_config::parse_form("password=secret12", credentials);
  EXPECT_TRUE(!ok);
}

HOST_TEST(setup_ap_ssid_uses_last_three_mac_bytes_uppercase) {
  const std::array<uint8_t, 6> mac{0xDE, 0xAD, 0xBE, 0xA1, 0xB2, 0xC3};
  EXPECT_EQ(wifi_config::setup_ap_ssid(mac), std::string("RLCD-A1B2C3"));
}

HOST_TEST(portal_qr_payload_appends_password_as_query_param) {
  EXPECT_EQ(wifi_config::portal_qr_payload("http://192.168.4.1/", "aB3dEfGh"),
            std::string("http://192.168.4.1/?pw=aB3dEfGh"));
}

HOST_TEST(portal_qr_payload_returns_bare_url_when_password_empty) {
  EXPECT_EQ(wifi_config::portal_qr_payload("http://192.168.4.1/", ""),
            std::string("http://192.168.4.1/"));
}

HOST_TEST(find_form_value_extracts_named_key_from_query_or_body_syntax) {
  std::string value;
  EXPECT_TRUE(wifi_config::find_form_value("pw=aB3dEfGh", "pw", value));
  EXPECT_EQ(value, std::string("aB3dEfGh"));
  EXPECT_TRUE(wifi_config::find_form_value("ssid=My+Net&pw=hunter2%21", "pw", value));
  EXPECT_EQ(value, std::string("hunter2!"));
}

HOST_TEST(find_form_value_returns_false_when_key_is_absent) {
  std::string value;
  EXPECT_TRUE(!wifi_config::find_form_value("ssid=Net", "pw", value));
}

HOST_TEST(constant_time_equal_matches_identical_strings) {
  EXPECT_TRUE(wifi_config::constant_time_equal("aB3dEfGh", "aB3dEfGh"));
}

HOST_TEST(constant_time_equal_rejects_different_strings_same_length) {
  EXPECT_TRUE(!wifi_config::constant_time_equal("aB3dEfGh", "aB3dEfGx"));
}

HOST_TEST(constant_time_equal_rejects_different_lengths) {
  EXPECT_TRUE(!wifi_config::constant_time_equal("short", "muchlonger"));
  EXPECT_TRUE(!wifi_config::constant_time_equal("", "aB3dEfGh"));
}

HOST_TEST(format_passphrase_has_pinned_length) {
  EXPECT_EQ(wifi_config::kPassphraseLength, static_cast<std::size_t>(8));
  const std::array<uint8_t, wifi_config::kPassphraseLength> bytes{
      0, 1, 2, 3, 4, 5, 6, 7};
  EXPECT_EQ(wifi_config::format_passphrase(bytes.data(), bytes.size()).size(),
            wifi_config::kPassphraseLength);
}

HOST_TEST(format_passphrase_uses_only_unambiguous_alphabet) {
  const std::string alphabet =
      "23456789abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ";
  std::array<uint8_t, wifi_config::kPassphraseLength> bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<uint8_t>(i * 37 + 5);
  }
  const std::string passphrase =
      wifi_config::format_passphrase(bytes.data(), bytes.size());
  for (const char c : passphrase) {
    EXPECT_TRUE(alphabet.find(c) != std::string::npos);
    EXPECT_TRUE(c != '0' && c != 'O' && c != '1' && c != 'l' && c != 'I');
  }
}

HOST_TEST(format_passphrase_is_deterministic_for_identical_input) {
  const std::array<uint8_t, wifi_config::kPassphraseLength> bytes{
      9, 8, 7, 6, 5, 4, 3, 2};
  EXPECT_EQ(wifi_config::format_passphrase(bytes.data(), bytes.size()),
            wifi_config::format_passphrase(bytes.data(), bytes.size()));
}

HOST_TEST(format_passphrase_differs_for_different_input) {
  const std::array<uint8_t, wifi_config::kPassphraseLength> a{
      1, 2, 3, 4, 5, 6, 7, 8};
  const std::array<uint8_t, wifi_config::kPassphraseLength> b{
      8, 7, 6, 5, 4, 3, 2, 1};
  EXPECT_TRUE(wifi_config::format_passphrase(a.data(), a.size()) !=
              wifi_config::format_passphrase(b.data(), b.size()));
}

HOST_TEST(format_passphrase_rejects_short_buffer) {
  const std::array<uint8_t, wifi_config::kPassphraseLength - 1> bytes{
      1, 2, 3, 4, 5, 6, 7};
  EXPECT_EQ(wifi_config::format_passphrase(bytes.data(), bytes.size()),
            std::string(""));
  EXPECT_EQ(wifi_config::format_passphrase(nullptr, 0), std::string(""));
}

HOST_TEST(state_machine_starts_connecting_when_credentials_saved) {
  StateMachine machine(true);
  EXPECT_TRUE(machine.state() == StateMachine::State::Connecting);
}

HOST_TEST(state_machine_starts_setup_ap_without_saved_credentials) {
  StateMachine machine(false);
  EXPECT_TRUE(machine.state() == StateMachine::State::SetupAp);
}

HOST_TEST(state_machine_moves_to_setup_ap_after_max_retries) {
  StateMachine machine(true);
  for (int i = 0; i < StateMachine::kMaxRetries - 1; ++i) {
    machine.on_disconnected();
    EXPECT_TRUE(machine.state() == StateMachine::State::Connecting);
  }
  EXPECT_EQ(machine.retries(), StateMachine::kMaxRetries - 1);
  machine.on_disconnected();
  EXPECT_TRUE(machine.state() == StateMachine::State::SetupAp);
  EXPECT_EQ(machine.retries(), StateMachine::kMaxRetries);
}

HOST_TEST(state_machine_connected_resets_retry_counter) {
  StateMachine machine(true);
  machine.on_disconnected();
  machine.on_disconnected();
  machine.on_connected();
  EXPECT_TRUE(machine.state() == StateMachine::State::Connected);
  EXPECT_EQ(machine.retries(), 0);
}

HOST_TEST(state_machine_disconnect_from_connected_returns_to_connecting) {
  StateMachine machine(true);
  machine.on_connected();
  machine.on_disconnected();
  EXPECT_TRUE(machine.state() == StateMachine::State::Connecting);
  EXPECT_EQ(machine.retries(), 0);
}

HOST_TEST(state_machine_ignores_disconnect_while_in_setup_ap) {
  StateMachine machine(false);
  machine.on_disconnected();
  EXPECT_TRUE(machine.state() == StateMachine::State::SetupAp);
  EXPECT_EQ(machine.retries(), 0);
}

HOST_TEST(state_machine_gesture_toggles_to_setup_ap_from_connecting_and_connected) {
  StateMachine connecting_machine(true);
  connecting_machine.on_setup_gesture();
  EXPECT_TRUE(connecting_machine.state() == StateMachine::State::SetupAp);

  StateMachine connected_machine(true);
  connected_machine.on_connected();
  connected_machine.on_setup_gesture();
  EXPECT_TRUE(connected_machine.state() == StateMachine::State::SetupAp);
}

HOST_TEST(state_machine_gesture_from_setup_ap_returns_to_connecting_with_credentials) {
  StateMachine machine(true);
  machine.on_setup_gesture();
  EXPECT_TRUE(machine.state() == StateMachine::State::SetupAp);
  machine.on_setup_gesture();
  EXPECT_TRUE(machine.state() == StateMachine::State::Connecting);
}

HOST_TEST(state_machine_gesture_from_setup_ap_stays_without_credentials) {
  StateMachine machine(false);
  machine.on_setup_gesture();
  EXPECT_TRUE(machine.state() == StateMachine::State::SetupAp);
}

HOST_TEST(state_machine_credentials_saved_moves_to_connecting_with_fresh_retries) {
  StateMachine machine(false);
  machine.on_setup_gesture();
  EXPECT_TRUE(machine.state() == StateMachine::State::SetupAp);
  machine.on_credentials_saved();
  EXPECT_TRUE(machine.state() == StateMachine::State::Connecting);
  EXPECT_EQ(machine.retries(), 0);

  machine.on_disconnected();
  machine.on_disconnected();
  machine.on_credentials_saved();
  EXPECT_EQ(machine.retries(), 0);
  EXPECT_TRUE(machine.state() == StateMachine::State::Connecting);
}

HOST_TEST(state_machine_reconnect_after_saving_credentials_then_disconnect) {
  StateMachine machine(false);
  machine.on_credentials_saved();
  EXPECT_TRUE(machine.state() == StateMachine::State::Connecting);
  for (int i = 0; i < StateMachine::kMaxRetries; ++i) {
    machine.on_disconnected();
  }
  EXPECT_TRUE(machine.state() == StateMachine::State::SetupAp);
}

// wifi_provision.cpp's connect-timeout esp_timer (armed whenever this state
// machine is Connecting; see apply_state_and_publish()) fires
// handle_wifi_disconnected(DisconnectReason::Timeout) on expiry, which calls
// this exact on_disconnected() - the state machine takes no reason
// parameter, so a timed-out attempt counts against the retry budget exactly
// like a real WIFI_EVENT_STA_DISCONNECTED does, and five of them (real or
// timed-out) land in SetupAp the same way.
HOST_TEST(state_machine_on_disconnected_is_reason_agnostic_five_reach_setup_ap) {
  StateMachine machine(true);
  for (int i = 0; i < StateMachine::kMaxRetries; ++i) {
    EXPECT_TRUE(machine.state() == StateMachine::State::Connecting);
    machine.on_disconnected();
  }
  EXPECT_TRUE(machine.state() == StateMachine::State::SetupAp);
  EXPECT_EQ(machine.retries(), StateMachine::kMaxRetries);
}
