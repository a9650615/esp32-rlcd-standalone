#include "internal.hpp"

#include <esp_app_desc.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <cstring>

#include "dns_server.h"
#include "ota_confirm.hpp"
#include "ota_image.hpp"
#include "ota_pull.hpp"
#include "ota_release.hpp"
#include "ota_session.hpp"
#include "wifi_provision.hpp"

#include <memory>
#include <string_view>

namespace wifi_provision {
namespace {

constexpr char kTag[] = "portal";
constexpr size_t kMaxFormBody = 512;
// "pw=" + an 8-char password + slack; generous enough that a too-long query
// just fails httpd_req_get_url_query_str() and falls through to "wrong
// password" rather than truncating into a false match.
constexpr size_t kMaxQuery = 32;
// Five minutes: long enough to start a push from another room and then walk
// to the board, which is the actual situation. The earlier 45 seconds assumed
// someone standing over it. Still bounded, because an unanswered push holds a
// socket and keeps the prompt on screen.
constexpr uint32_t kOtaConfirmTimeoutMs = 5 * 60'000;

httpd_handle_t server_ = nullptr;
dns_server_handle_t dns_ = nullptr;

const char* current_app_version() {
  const esp_app_desc_t* desc = esp_app_get_description();
  return desc != nullptr ? desc->version : "unknown";
}

// Puts a failed update on the panel. The phone gets the same reason in its
// HTTP response, but the two audiences are not the same: whoever is standing
// in front of the board may not be the one holding the phone, and after a
// reboot the response is gone while the panel still says what happened.
void report_ota_failure(const char* reason) {
  app_core::OtaData data;
  data.phase = app_core::OtaPhase::Failed;
  data.detail = reason;
  set_ota(data);
}

void restart_timer_fired(void*) { esp_restart(); }

// Rebooting inside the request handler drops the socket mid-response, so the
// phone reports a network error for an update that in fact succeeded. Answer
// first, give the response time to drain, then restart.
void schedule_restart() {
  static esp_timer_handle_t timer = nullptr;
  if (timer != nullptr) return;
  esp_timer_create_args_t args{};
  args.callback = &restart_timer_fired;
  args.name = "ota_restart";
  if (esp_timer_create(&args, &timer) != ESP_OK) {
    ESP_LOGE(kTag, "restart timer unavailable; restarting immediately");
    esp_restart();
  }
  (void)esp_timer_start_once(timer, 1'500'000);
}

std::string error_text(wifi_config::CredentialError error) {
  switch (error) {
    case wifi_config::CredentialError::SsidEmpty:
      return "Please enter a network name.";
    case wifi_config::CredentialError::SsidTooLong:
      return "Network name is too long.";
    case wifi_config::CredentialError::PasswordTooShort:
      return "Password must be at least 8 characters (or blank for an open "
             "network).";
    case wifi_config::CredentialError::PasswordTooLong:
      return "Password is too long.";
    case wifi_config::CredentialError::None:
      return {};
  }
  return {};
}

// `pw` is the already-authenticated page password, threaded through as a
// hidden field so the POST that follows carries it too. It only ever holds
// characters from wifi_config's passphrase alphabet (alnum), so it is safe
// to splice into an HTML attribute without escaping.
std::string render_form_page(const std::string& error, const std::string& pw) {
  std::string html =
      "<!DOCTYPE html><html><head><title>RLCD Setup</title></head><body>";
  html += "<h1>" + current_ap_ssid() + "</h1>";
  html += "<p>" + current_status_text() + "</p>";
  if (!error.empty()) html += "<p><b>" + error + "</b></p>";
  html +=
      "<form method=\"POST\" action=\"/\">"
      "<input type=\"hidden\" name=\"pw\" value=\"" + pw + "\">"
      "SSID: <input name=\"ssid\" maxlength=\"32\"><br>"
      "Password: <input name=\"password\" type=\"password\" "
      "maxlength=\"63\"><br>"
      "<input type=\"submit\" value=\"Connect\"></form>";
  html += "<hr><p><a href=\"/update?pw=" + pw +
          "\">Firmware update</a> (running " +
          std::string(current_app_version()) + ")</p>";
  html += "</body></html>";
  return html;
}

// Its own page rather than more rows on the setup form. The three ways in -
// check a release, upload a file, fetch a URL - are each a few controls, and
// stacked under the Wi-Fi form on a phone they pushed the thing most people
// came for off the screen.
//
// `release` is the result of a check the user asked for, empty on first view;
// nothing is fetched just because the page was opened.
std::string render_update_page(const std::string& pw,
                               const std::string& release_message,
                               const std::string& release_url,
                               const std::string& error) {
  std::string html =
      "<!DOCTYPE html><html><head><title>RLCD Firmware</title>"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "</head><body>";
  html += "<h1>Firmware</h1>";
  html += "<p>Running version <b>" + std::string(current_app_version()) +
          "</b></p>";
  if (!error.empty()) html += "<p><b>" + error + "</b></p>";

  html += "<h2>Check for a release</h2>";
  if (!release_message.empty()) html += "<p>" + release_message + "</p>";
  html +=
      "<form method=\"POST\" action=\"/update\">"
      "<input type=\"hidden\" name=\"pw\" value=\"" + pw + "\">"
      "<input type=\"hidden\" name=\"action\" value=\"check\">"
      "<input type=\"submit\" value=\"Check now\"></form>";
  // Only offered once a check actually found a newer build with an asset, so
  // the install button cannot be pressed on a guess.
  if (!release_url.empty()) {
    html +=
        "<form method=\"POST\" action=\"/ota-url\">"
        "<input type=\"hidden\" name=\"pw\" value=\"" + pw + "\">"
        "<input type=\"hidden\" name=\"url\" value=\"" + release_url + "\">"
        "<input type=\"submit\" value=\"Install this release\"></form>";
  }

  html +=
      "<h2>Upload a file</h2>"
      "<form method=\"POST\" action=\"/ota?pw=" + pw + "\" id=\"f\">"
      "<input type=\"file\" id=\"b\" accept=\".bin\"><br>"
      "<input type=\"submit\" value=\"Upload and restart\"></form>"
      // Sends the file as the raw request body. A few lines of script instead
      // of a multipart parser on a device with no RAM to buffer the image.
      "<script>document.getElementById('f').onsubmit=function(e){"
      "e.preventDefault();var f=document.getElementById('b').files[0];"
      "if(!f)return;fetch(this.action,{method:'POST',body:f})"
      ".then(r=>r.text()).then(t=>{document.body.innerHTML='<p>'+t+'</p>';});"
      "};</script>";

  html +=
      "<h2>Fetch a URL</h2>"
      "<form method=\"POST\" action=\"/ota-url\">"
      "<input type=\"hidden\" name=\"pw\" value=\"" + pw + "\">"
      "<input name=\"url\" size=\"40\" placeholder=\"https://...\"><br>"
      "<input type=\"submit\" value=\"Download and restart\"></form>";

  html += "<p>Progress and any failure also appear on the device's screen.</p>";
  html += "<p><a href=\"/?pw=" + pw + "\">Back to setup</a></p>";
  html += "</body></html>";
  return html;
}

// Gate in front of render_form_page(): shown for any GET/POST that doesn't
// carry the correct page password. Never the SSID form and never an HTTP
// error - a captive-portal probe (redirected here with no ?pw=) must land
// on something a phone's webview renders normally.
std::string render_password_page(bool wrong) {
  std::string html =
      "<!DOCTYPE html><html><head><title>RLCD Setup</title></head><body>";
  html += "<h1>" + current_ap_ssid() + "</h1>";
  if (wrong) html += "<p><b>Wrong password.</b></p>";
  html +=
      "<form method=\"POST\" action=\"/\">"
      "Password: <input name=\"pw\" type=\"password\"><br>"
      "<input type=\"submit\" value=\"Continue\"></form></body></html>";
  return html;
}

// Laziest thing that works: no JS, no polling endpoint, just a timed
// redirect back to "/". If the credentials were good, the board tears the
// AP down before this fires and the phone simply drops off the network -
// the text below sets that expectation. If they were bad, "/" is still
// being served and will render the real failure reason plus the form again.
std::string render_connecting_page() {
  return "<!DOCTYPE html><html><head><title>RLCD Setup</title>"
         "<meta http-equiv=\"refresh\" content=\"5;url=/\"></head><body>"
         "<p>Connecting to your network...</p>"
         "<p>This page will check again in a few seconds. If the network "
         "was found, this page will stop responding and the display on "
         "the device will show the result.</p></body></html>";
}

void send_html(httpd_req_t* req, const std::string& html) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html.c_str(), html.size());
}

// req->uri is the ESP-IDF httpd server's raw request-line URI - it includes
// the query string verbatim (see httpd_parse.c), so it must never be handed
// to ESP_LOG* on either root route: the page password can ride in ?pw=...
// and would otherwise land in the log. Both routes are registered on the
// literal path "/", so log sites below use that constant instead of
// req->uri; only find_form_value()/constant_time_equal() ever see the value.
// Every password check goes through here, and an unset password authorises
// nothing. constant_time_equal("", "") is true, so with the portal reachable
// while the board is on the home network - where no session password exists -
// a bare request would otherwise have opened the Wi-Fi credential form to the
// whole LAN.
bool portal_password_ok(const std::string& candidate) {
  const std::string& expected = current_portal_password();
  if (expected.empty()) return false;
  return wifi_config::constant_time_equal(candidate, expected);
}

std::string query_pw(httpd_req_t* req) {
  char query[kMaxQuery];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    return {};
  }
  std::string pw;
  wifi_config::find_form_value(query, "pw", pw);
  return pw;
}

esp_err_t root_get_handler(httpd_req_t* req) {
  const std::string pw = query_pw(req);
  if (portal_password_ok(pw)) {
    ESP_LOGI(kTag, "GET / -> setup form shown");
    send_html(req, render_form_page({}, pw));
    return ESP_OK;
  }
  ESP_LOGI(kTag, "GET / -> password prompt");
  send_html(req, render_password_page(false));
  return ESP_OK;
}

esp_err_t root_post_handler(httpd_req_t* req) {
  // The spec allows the password as a ?pw= query parameter too, not just
  // the hidden form field, so check that first.
  const std::string query_password = query_pw(req);

  if (req->content_len == 0) {
    ESP_LOGW(kTag, "POST / -> empty body, password prompt");
    if (portal_password_ok(query_password)) {
      send_html(req, render_form_page({}, query_password));
    } else {
      send_html(req, render_password_page(true));
    }
    return ESP_OK;
  }
  if (req->content_len >= kMaxFormBody) {
    ESP_LOGW(kTag, "POST / -> validation error: body too large");
    send_html(req, render_password_page(true));
    return ESP_OK;
  }

  char buffer[kMaxFormBody];
  size_t received = 0;
  while (received < req->content_len) {
    const int chunk = httpd_req_recv(req, buffer + received,
                                     req->content_len - received);
    if (chunk == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (chunk <= 0) return ESP_FAIL;
    received += static_cast<size_t>(chunk);
  }
  buffer[received] = '\0';
  const std::string_view body(buffer, received);

  std::string body_pw;
  wifi_config::find_form_value(body, "pw", body_pw);
  const std::string& pw = !query_password.empty() ? query_password : body_pw;
  if (!portal_password_ok(pw)) {
    ESP_LOGW(kTag, "POST / -> wrong page password");
    send_html(req, render_password_page(true));
    return ESP_OK;
  }

  wifi_config::Credentials creds;
  if (!wifi_config::parse_form(body, creds)) {
    // Correct password but no ssid field: this was the password-prompt
    // form's own submission. Show the real setup form next.
    send_html(req, render_form_page({}, pw));
    return ESP_OK;
  }

  const wifi_config::CredentialError error = wifi_config::validate(creds);
  if (error != wifi_config::CredentialError::None) {
    // creds.ssid is public (broadcast in the clear); the password itself is
    // never logged, only whether it passed validation.
    ESP_LOGW(kTag, "POST / -> validation error: %s", error_text(error).c_str());
    send_html(req, render_form_page(error_text(error), pw));
    return ESP_OK;
  }

  ESP_LOGI(kTag, "POST / -> credentials accepted, ssid=%s", creds.ssid.c_str());
  handle_credentials_saved(creds);
  send_html(req, render_connecting_page());
  return ESP_OK;
}

// iOS's captive-portal webview requires response content to recognize a
// redirect; an empty 30x is not sufficient.
esp_err_t redirect_404_handler(httpd_req_t* req, httpd_err_code_t) {
  // Captive-portal probing hits this constantly (every associated client,
  // repeatedly); debug level keeps it out of the way while still available.
  // This handler fires for paths other than "/", so unlike the root routes
  // above the path itself is worth logging - but the query string still
  // isn't (a probe could carry an incidental ?pw=... on the way to 404), so
  // strip it rather than log req->uri whole.
  const std::string_view uri(req->uri);
  const std::size_t query_start = uri.find('?');
  ESP_LOGD(kTag, "GET %.*s -> 404, redirecting to /",
          static_cast<int>(uri.substr(0, query_start).size()), req->uri);
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", "/");
  httpd_resp_send(req, "Redirecting to setup", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Streams the request body straight into the inactive OTA slot. No multipart
// parsing and no staging file: this device has neither the RAM to hold a
// 1.5 MB image nor a filesystem to spill it to, so the bytes go from socket to
// flash as they arrive.
//
// The password is checked before a single byte is read. Everything after that
// is ota::Session's problem - it refuses to erase anything until the header
// proves the upload is this project's firmware.
esp_err_t ota_post_handler(httpd_req_t* req) {
  // Held across the confirmation so the bytes already off the wire are fed to
  // the Session once it exists. They must reach flash exactly once - the same
  // rule PrefixInspector follows internally.
  uint8_t preamble[ota::kImagePrefixBytes];
  std::size_t preamble_len = 0;
  // Either the setup page's session password, or - when there is no session,
  // which is the normal state on the home network - somebody at the board
  // saying yes. The endpoint listens whenever the board is online so nothing
  // has to be pressed to make a push possible, but a device that reflashes
  // itself because a request arrived is one anyone on the LAN can reflash.
  if (!portal_password_ok(query_pw(req))) {
    // Naming the source makes the prompt about a specific request rather than
    // an abstract one. esp_http_server exposes the socket; the address comes
    // from the standard call on it.
    char peer[48] = "the network";
    const int sock = httpd_req_to_sockfd(req);
    if (sock >= 0) {
      struct sockaddr_in6 source {};
      socklen_t length = sizeof(source);
      if (getpeername(sock, reinterpret_cast<struct sockaddr*>(&source),
                      &length) == 0) {
        inet_ntop(AF_INET6, &source.sin6_addr, peer, sizeof(peer));
        // lwIP hands back IPv4 clients as ::FFFF:a.b.c.d; show the part
        // someone would recognise as their machine.
        const char* mapped = std::strrchr(peer, ':');
        if (mapped != nullptr && std::strchr(peer, '.') != nullptr) {
          std::memmove(peer, mapped + 1, std::strlen(mapped));
        }
      }
    }
    // Read the descriptor before asking. The prompt is where someone decides
    // whether to replace the firmware, and "something is being pushed from
    // 192.168.3.111" is not enough to decide with - the version is, and the
    // image carries it in its first 112 bytes.
    //
    // Safe to do before the answer: nothing is erased and nothing is written
    // until a Session exists, and none exists yet. Reading is not committing.
    //
    // It also means a file that is not firmware at all is refused without
    // putting a prompt on the panel for someone to walk over and answer.
    while (preamble_len < ota::kImagePrefixBytes &&
           preamble_len < req->content_len) {
      const int received = httpd_req_recv(
          req, reinterpret_cast<char*>(preamble) + preamble_len,
          ota::kImagePrefixBytes - preamble_len);
      if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
      if (received <= 0) {
        ESP_LOGE(kTag, "POST /ota -> transfer died before the header arrived");
        return ESP_FAIL;
      }
      preamble_len += static_cast<std::size_t>(received);
    }
    const ota::ImageInfo info =
        ota::inspect_image_prefix(preamble, preamble_len);
    if (info.verdict != ota::ImageVerdict::Ok) {
      const char* reason = ota::image_verdict_message(info.verdict);
      ESP_LOGE(kTag, "POST /ota -> refused before prompting: %s", reason);
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, reason);
      return ESP_OK;
    }

    const ota::ConfirmResult answer =
        ota::request_confirm(peer, info.version, kOtaConfirmTimeoutMs);
    if (answer != ota::ConfirmResult::Accepted) {
      ESP_LOGW(kTag, "POST /ota -> not confirmed at the board");
      httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                          "Not confirmed on the device");
      return ESP_OK;
    }
  }

  // content_len of 0 means the client sent no length, in which case the panel
  // shows WORKING rather than a fabricated percentage.
  ota::Session session(static_cast<std::size_t>(req->content_len));
  ESP_LOGW(kTag, "POST /ota -> firmware upload starting, %u bytes declared",
           static_cast<unsigned>(req->content_len));

  // 4 KiB: one flash page's worth per recv, small enough to stay off the
  // handler task's stack budget alongside mbedTLS.
  static constexpr std::size_t kChunkBytes = 4096;
  auto buffer = std::make_unique<uint8_t[]>(kChunkBytes);
  std::size_t remaining = req->content_len;

  // Whatever was read to identify the image goes in first, in order, before
  // anything still on the wire.
  if (preamble_len > 0) {
    const esp_err_t written = session.write(preamble, preamble_len);
    if (written != ESP_OK) {
      const char* reason = written == ESP_ERR_INVALID_VERSION
                               ? ota::image_verdict_message(session.verdict())
                               : esp_err_to_name(written);
      ESP_LOGE(kTag, "POST /ota -> rejected: %s", reason);
      report_ota_failure(reason);
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, reason);
      return ESP_OK;
    }
    remaining -= preamble_len;
  }
  while (remaining > 0) {
    const int received = httpd_req_recv(
        req, reinterpret_cast<char*>(buffer.get()),
        remaining < kChunkBytes ? remaining : kChunkBytes);
    if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (received <= 0) {
      ESP_LOGE(kTag, "POST /ota -> transfer died with %u bytes left",
               static_cast<unsigned>(remaining));
      // Session's destructor aborts, so the half-written slot is released.
      report_ota_failure("Upload interrupted");
      return ESP_FAIL;
    }
    const esp_err_t written =
        session.write(buffer.get(), static_cast<std::size_t>(received));
    if (written != ESP_OK) {
      const char* reason = written == ESP_ERR_INVALID_VERSION
                               ? ota::image_verdict_message(session.verdict())
                               : esp_err_to_name(written);
      ESP_LOGE(kTag, "POST /ota -> rejected: %s", reason);
      report_ota_failure(reason);
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, reason);
      return ESP_OK;
    }
    remaining -= static_cast<std::size_t>(received);
  }

  const esp_err_t finished = session.finish();
  if (finished != ESP_OK) {
    const char* reason = finished == ESP_ERR_INVALID_SIZE
                             ? "Upload ended before a complete header"
                             : "Image failed its own integrity check";
    ESP_LOGE(kTag, "POST /ota -> %s (%s)", reason, esp_err_to_name(finished));
    report_ota_failure(reason);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, reason);
    return ESP_OK;
  }

  httpd_resp_sendstr(req,
                     "Firmware accepted. The device is restarting and will "
                     "verify the new image; if it fails, it rolls back on its "
                     "own. Watch the display.");
  // Answer first, then restart: rebooting inside the handler drops the socket
  // and the phone shows a network error for an update that actually worked.
  schedule_restart();
  return ESP_OK;
}

esp_err_t ota_url_post_handler(httpd_req_t* req) {
  if (req->content_len == 0 || req->content_len >= kMaxFormBody) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
    return ESP_OK;
  }
  char buffer[kMaxFormBody];
  size_t received = 0;
  while (received < req->content_len) {
    const int chunk =
        httpd_req_recv(req, buffer + received, req->content_len - received);
    if (chunk == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (chunk <= 0) return ESP_FAIL;
    received += static_cast<size_t>(chunk);
  }
  const std::string_view body(buffer, received);

  std::string body_pw;
  wifi_config::find_form_value(body, "pw", body_pw);
  const std::string query_password = query_pw(req);
  const std::string& pw = !query_password.empty() ? query_password : body_pw;
  if (!portal_password_ok(pw)) {
    ESP_LOGW(kTag, "POST /ota-url -> wrong page password");
    send_html(req, render_password_page(true));
    return ESP_OK;
  }

  std::string url;
  wifi_config::find_form_value(body, "url", url);
  if (url.empty()) {
    send_html(req, render_form_page("Enter a firmware URL.", pw));
    return ESP_OK;
  }

  // Same downloader the settings row runs, so the two cannot drift.
  if (!ota::start_pull(url)) {
    ESP_LOGE(kTag, "POST /ota-url -> could not start the download task");
    send_html(req, render_form_page("Device busy; try again.", pw));
    return ESP_OK;
  }

  ESP_LOGW(kTag, "POST /ota-url -> download started");
  httpd_resp_sendstr(req,
                     "Downloading firmware. Watch the display for progress. "
                     "The device restarts on its own if the image is good, "
                     "and stays on the current firmware if it is not.");
  return ESP_OK;
}

esp_err_t update_get_handler(httpd_req_t* req) {
  const std::string pw = query_pw(req);
  if (!portal_password_ok(pw)) {
    send_html(req, render_password_page(false));
    return ESP_OK;
  }
  // No check is run on load: opening the page should not reach out to the
  // network, and a rate-limited API answer would be a confusing thing to greet
  // someone with.
  send_html(req, render_update_page(pw, {}, {}, {}));
  return ESP_OK;
}

// Runs the release query on the request thread rather than a task, unlike the
// download: this is one small HTTPS GET, and the answer is what the page is
// being re-rendered to show.
esp_err_t update_post_handler(httpd_req_t* req) {
  if (req->content_len == 0 || req->content_len >= kMaxFormBody) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
    return ESP_OK;
  }
  char buffer[kMaxFormBody];
  size_t received = 0;
  while (received < req->content_len) {
    const int chunk =
        httpd_req_recv(req, buffer + received, req->content_len - received);
    if (chunk == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (chunk <= 0) return ESP_FAIL;
    received += static_cast<size_t>(chunk);
  }
  const std::string_view body(buffer, received);

  std::string body_pw;
  wifi_config::find_form_value(body, "pw", body_pw);
  const std::string query_password = query_pw(req);
  const std::string& pw = !query_password.empty() ? query_password : body_pw;
  if (!portal_password_ok(pw)) {
    ESP_LOGW(kTag, "POST /update -> wrong page password");
    send_html(req, render_password_page(true));
    return ESP_OK;
  }

  const ota::ReleaseInfo release = ota::check_latest_release();
  // The install button is offered only for a release that both parsed as newer
  // and actually carries firmware; check_latest_release decides that, and this
  // does not second-guess it.
  const std::string url =
      release.update_available ? release.firmware_url : std::string();
  send_html(req, render_update_page(pw, release.message, url, {}));
  return ESP_OK;
}

const httpd_uri_t kUpdateGet = {"/update", HTTP_GET, update_get_handler,
                                nullptr};
const httpd_uri_t kUpdatePost = {"/update", HTTP_POST, update_post_handler,
                                 nullptr};
const httpd_uri_t kOtaPost = {"/ota", HTTP_POST, ota_post_handler, nullptr};
const httpd_uri_t kOtaUrlPost = {"/ota-url", HTTP_POST, ota_url_post_handler,
                                 nullptr};
#ifndef NDEBUG
// 400x300, 1 bit per pixel. Static rather than stack: this runs on the HTTP
// task, and 15 KiB is more than it has.
constexpr size_t kShotBytes = 400 * 300 / 8;
constexpr size_t kShotChunk = 96;  // 96 raw bytes -> 128 base64 chars
bool (*g_shot_provider)(uint8_t*, size_t) = nullptr;

// The panel's current contents, in the same "SHOT <base64>" line format the
// once-per-boot log dump uses, so scripts/decode-screenshots.py reads either
// source without knowing which it got.
//
// On demand rather than once per boot, because the boot-time dump is only
// reachable by rebooting the board - which, with no cable attached, means
// pushing firmware just to see a layout.
esp_err_t shot_get_handler(httpd_req_t* req) {
  // The setup page prints the portal password on the panel. A screenshot route
  // that answered during setup would hand that password to anyone on the LAN,
  // which is the one thing the password exists to prevent. Non-empty means
  // setup is active - see current_portal_password.
  if (!current_portal_password().empty()) {
    ESP_LOGW(kTag, "GET /shot -> refused while the setup page is showing");
    httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                        "Refused: the setup page shows the portal password.");
    return ESP_OK;
  }
  if (g_shot_provider == nullptr) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "No screenshot provider registered");
    return ESP_OK;
  }

  static uint8_t raw[kShotBytes];
  if (!g_shot_provider(raw, sizeof(raw))) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Framebuffer not readable yet");
    return ESP_OK;
  }

  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr_chunk(req, "SHOT BEGIN Live 400x300\n");
  for (size_t offset = 0; offset < sizeof(raw); offset += kShotChunk) {
    const size_t chunk = std::min(kShotChunk, sizeof(raw) - offset);
    unsigned char encoded[kShotChunk * 4 / 3 + 8];
    size_t written = 0;
    if (mbedtls_base64_encode(encoded, sizeof(encoded), &written, raw + offset,
                              chunk) != 0) {
      // Abandon the chunked response rather than finish it: a truncated body
      // that ends cleanly would decode into a picture missing its bottom.
      ESP_LOGE(kTag, "GET /shot -> encode failed at %u", (unsigned)offset);
      return ESP_FAIL;
    }
    encoded[written] = '\0';
    httpd_resp_sendstr_chunk(req, "SHOT ");
    httpd_resp_sendstr_chunk(req, reinterpret_cast<char*>(encoded));
    httpd_resp_sendstr_chunk(req, "\n");
  }
  httpd_resp_sendstr_chunk(req, "SHOT END Live\n");
  httpd_resp_sendstr_chunk(req, nullptr);
  return ESP_OK;
}

const httpd_uri_t kShotGet = {"/shot", HTTP_GET, shot_get_handler, nullptr};

// Reboots the board. The last thing that still required a USB cable: verifying
// anything that only happens at startup - a setting restored from NVS, the
// boot order, a first-render layout - meant reaching for serial, and reaching
// for serial to reset means find-board-port.sh, which stops the application on
// every port it probes.
//
// POST, not GET: a GET that reboots the device is one browser prefetch or one
// pasted link away from firing by accident.
//
// Unauthenticated, like every other route here, and debug builds only. A push
// needs a button because it replaces the firmware; a restart cannot corrupt
// anything and the running image comes straight back, so gating it behind a
// press would only mean the cable stays.
esp_err_t restart_post_handler(httpd_req_t* req) {
  ESP_LOGW(kTag, "POST /restart -> restarting on request");
  // Answer first, then restart: rebooting inside the handler drops the socket
  // and the caller sees a network error for a reboot that worked. Same reason
  // the OTA handler does it in this order.
  httpd_resp_sendstr(req, "Restarting.\n");
  schedule_restart();
  return ESP_OK;
}

const httpd_uri_t kRestartPost = {"/restart", HTTP_POST, restart_post_handler,
                                  nullptr};
#endif

const httpd_uri_t kRootGet = {"/", HTTP_GET, root_get_handler, nullptr};
const httpd_uri_t kRootPost = {"/", HTTP_POST, root_post_handler, nullptr};

}  // namespace

void portal_start() {
  // Captive-portal probing generates a lot of expected 404s; keep them quiet.
  esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
  esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
  esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;
  // Sized to the table below with room to grow. The default is 8, and
  // registration past the limit fails by returning an error rather than by
  // complaining, so a route added as the ninth would simply 404 with nothing
  // to explain why.
  config.max_uri_handlers = 12;
  // A firmware upload holds the socket for the length of the transfer, and a
  // release check waits on GitHub's TLS handshake.
  config.recv_wait_timeout = 20;
  config.send_wait_timeout = 20;
  if (httpd_start(&server_, &config) != ESP_OK) {
    ESP_LOGE(kTag, "httpd_start failed; the setup portal will not answer");
    return;
  }

  const httpd_uri_t* routes[] = {&kRootGet,   &kRootPost,  &kOtaPost,
                                 &kOtaUrlPost, &kUpdateGet, &kUpdatePost,
#ifndef NDEBUG
                                 &kShotGet,   &kRestartPost,
#endif
  };
  for (const httpd_uri_t* route : routes) {
    const esp_err_t result = httpd_register_uri_handler(server_, route);
    if (result != ESP_OK) {
      ESP_LOGE(kTag, "route %s unavailable: %s", route->uri,
               esp_err_to_name(result));
    }
  }
  httpd_register_err_handler(server_, HTTPD_404_NOT_FOUND,
                             &redirect_404_handler);

  dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
  dns_ = start_dns_server(&dns_config);
}

void set_screenshot_provider(bool (*provider)(uint8_t* out, size_t length)) {
#ifndef NDEBUG
  g_shot_provider = provider;
#else
  // Release builds do not register the route, so there is nothing to answer.
  (void)provider;
#endif
}

void portal_stop() {
  if (server_ != nullptr) {
    httpd_stop(server_);
    server_ = nullptr;
  }
  if (dns_ != nullptr) {
    stop_dns_server(dns_);
    dns_ = nullptr;
  }
}

}  // namespace wifi_provision
