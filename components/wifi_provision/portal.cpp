#include "internal.hpp"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>

#include "dns_server.h"

#include <string_view>

namespace wifi_provision {
namespace {

constexpr char kTag[] = "portal";
constexpr size_t kMaxFormBody = 512;

httpd_handle_t server_ = nullptr;
dns_server_handle_t dns_ = nullptr;

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

std::string render_form_page(const std::string& error) {
  std::string html =
      "<!DOCTYPE html><html><head><title>RLCD Setup</title></head><body>";
  html += "<h1>" + current_ap_ssid() + "</h1>";
  html += "<p>" + current_status_text() + "</p>";
  if (!error.empty()) html += "<p><b>" + error + "</b></p>";
  html +=
      "<form method=\"POST\" action=\"/\">"
      "SSID: <input name=\"ssid\" maxlength=\"32\"><br>"
      "Password: <input name=\"password\" type=\"password\" "
      "maxlength=\"63\"><br>"
      "<input type=\"submit\" value=\"Connect\"></form></body></html>";
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

esp_err_t root_get_handler(httpd_req_t* req) {
  ESP_LOGI(kTag, "GET %s -> form shown", req->uri);
  send_html(req, render_form_page({}));
  return ESP_OK;
}

esp_err_t root_post_handler(httpd_req_t* req) {
  if (req->content_len == 0) {
    ESP_LOGW(kTag, "POST %s -> validation error: empty body", req->uri);
    send_html(req, render_form_page("Please enter a network name."));
    return ESP_OK;
  }
  if (req->content_len >= kMaxFormBody) {
    ESP_LOGW(kTag, "POST %s -> validation error: body too large", req->uri);
    send_html(req, render_form_page("Form submission is too large."));
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

  wifi_config::Credentials creds;
  if (!wifi_config::parse_form(std::string_view(buffer, received), creds)) {
    ESP_LOGW(kTag, "POST %s -> validation error: malformed form", req->uri);
    send_html(req, render_form_page("Malformed form submission."));
    return ESP_OK;
  }

  const wifi_config::CredentialError error = wifi_config::validate(creds);
  if (error != wifi_config::CredentialError::None) {
    // creds.ssid is public (broadcast in the clear); the password itself is
    // never logged, only whether it passed validation.
    ESP_LOGW(kTag, "POST %s -> validation error: %s", req->uri,
            error_text(error).c_str());
    send_html(req, render_form_page(error_text(error)));
    return ESP_OK;
  }

  ESP_LOGI(kTag, "POST %s -> credentials accepted, ssid=%s", req->uri,
          creds.ssid.c_str());
  handle_credentials_saved(creds);
  send_html(req, render_connecting_page());
  return ESP_OK;
}

// iOS's captive-portal webview requires response content to recognize a
// redirect; an empty 30x is not sufficient.
esp_err_t redirect_404_handler(httpd_req_t* req, httpd_err_code_t) {
  // Captive-portal probing hits this constantly (every associated client,
  // repeatedly); debug level keeps it out of the way while still available.
  ESP_LOGD(kTag, "GET %s -> 404, redirecting to /", req->uri);
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", "/");
  httpd_resp_send(req, "Redirecting to setup", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

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
