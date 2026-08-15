#include "internal.hpp"

#include <esp_app_desc.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "dns_server.h"
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
  // Firmware upload lives on the same authenticated page rather than a route
  // of its own: reaching here already required the KEY long-press that starts
  // setup mode plus the session password, and that pair is the access control
  // for flashing the device. enctype is deliberately absent - the handler
  // streams the raw body into flash, so a multipart wrapper would have to be
  // parsed back off on a device with no spare RAM to hold the whole image.
  html +=
      "<hr><h2>Firmware</h2>"
      "<p>Current: " + std::string(current_app_version()) + "</p>"
      "<form method=\"POST\" action=\"/ota?pw=" + pw + "\" "
      "enctype=\"text/plain\" id=\"f\">"
      "<input type=\"file\" id=\"b\" accept=\".bin\"><br>"
      "<input type=\"submit\" value=\"Upload and restart\"></form>"
      // Sends the file as the raw request body. Four lines of script instead
      // of a multipart parser in firmware.
      "<script>document.getElementById('f').onsubmit=function(e){"
      "e.preventDefault();var f=document.getElementById('b').files[0];"
      "if(!f)return;fetch(this.action,{method:'POST',body:f})"
      ".then(r=>r.text()).then(t=>{document.body.innerHTML='<p>'+t+'</p>';});"
      "};</script>";
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
  if (wifi_config::constant_time_equal(pw, current_portal_password())) {
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
    if (wifi_config::constant_time_equal(query_password, current_portal_password())) {
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
  if (!wifi_config::constant_time_equal(pw, current_portal_password())) {
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
  if (!wifi_config::constant_time_equal(query_pw(req),
                                        current_portal_password())) {
    ESP_LOGW(kTag, "POST /ota -> wrong page password");
    httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Wrong password");
    return ESP_OK;
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

const httpd_uri_t kOtaPost = {"/ota", HTTP_POST, ota_post_handler, nullptr};
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
  if (httpd_start(&server_, &config) == ESP_OK) {
    httpd_register_uri_handler(server_, &kRootGet);
    httpd_register_uri_handler(server_, &kRootPost);
    httpd_register_uri_handler(server_, &kOtaPost);
    httpd_register_err_handler(server_, HTTPD_404_NOT_FOUND,
                               &redirect_404_handler);
  } else {
    ESP_LOGW(kTag, "httpd_start failed");
  }

  dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
  dns_ = start_dns_server(&dns_config);
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
