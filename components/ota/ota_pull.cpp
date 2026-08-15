#include "ota_pull.hpp"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>

#include <memory>

#include "ota_session.hpp"

namespace ota {
namespace {

constexpr char kTag[] = "ota";
constexpr int kTimeoutMs = 15'000;
// 4 KiB per read: one flash page's worth, and small enough to sit alongside
// the mbedTLS handshake on the calling task's stack budget.
constexpr std::size_t kChunkBytes = 4096;

void report_failure(const char* reason) {
  app_core::OtaData data;
  data.phase = app_core::OtaPhase::Failed;
  data.detail = reason;
  publish_progress(data);
}

}  // namespace

PullResult pull_from_url(const std::string& url) {
  if (url.rfind("https://", 0) != 0) {
    // Plain HTTP would let anything on the path substitute the firmware. The
    // image validator would still reject a non-image, but it cannot tell a
    // genuine build from an attacker's genuine-looking one.
    ESP_LOGE(kTag, "refusing a non-HTTPS firmware URL");
    report_failure("Firmware URL must be https");
    return PullResult::InsecureUrl;
  }

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.timeout_ms = kTimeoutMs;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  // Release builds of a firmware host commonly redirect to a CDN or to a
  // versioned object; without this the body would be the redirect page and
  // the validator would (correctly, but confusingly) call it not-firmware.
  config.disable_auto_redirect = false;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    report_failure("Could not start the download");
    return PullResult::TransportFailed;
  }

  struct Closer {
    esp_http_client_handle_t client;
    ~Closer() {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
    }
  } closer{client};

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "firmware download could not connect: %s",
             esp_err_to_name(err));
    report_failure("Could not reach the firmware host");
    return PullResult::TransportFailed;
  }

  const int64_t content_length = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGE(kTag, "firmware host answered HTTP %d", status);
    report_failure("Firmware host returned an error");
    return PullResult::TransportFailed;
  }

  // A chunked response reports -1. That is workable - Session simply shows
  // WORKING instead of a percentage - so it is not treated as a failure.
  const std::size_t total =
      content_length > 0 ? static_cast<std::size_t>(content_length) : 0;
  ESP_LOGW(kTag, "pulling firmware, %s",
           total > 0 ? "size known" : "size unknown (chunked)");

  Session session(total);
  auto buffer = std::make_unique<uint8_t[]>(kChunkBytes);
  for (;;) {
    const int read = esp_http_client_read(
        client, reinterpret_cast<char*>(buffer.get()), kChunkBytes);
    if (read < 0) {
      ESP_LOGE(kTag, "firmware download broke off after %u bytes",
               static_cast<unsigned>(session.written()));
      report_failure("Download interrupted");
      return PullResult::TransportFailed;
    }
    if (read == 0) {
      // esp_http_client_read returns 0 at end of body, and also on a closed
      // connection mid-transfer. is_complete_data_received distinguishes the
      // two; without it a truncated download would be finalised as if whole
      // and only caught later by the image's own hash - if at all.
      if (!esp_http_client_is_complete_data_received(client)) {
        ESP_LOGE(kTag, "firmware download truncated at %u bytes",
                 static_cast<unsigned>(session.written()));
        report_failure("Download truncated");
        return PullResult::TransportFailed;
      }
      break;
    }
    const esp_err_t written =
        session.write(buffer.get(), static_cast<std::size_t>(read));
    if (written == ESP_ERR_INVALID_VERSION) {
      ESP_LOGE(kTag, "downloaded file rejected: %s",
               image_verdict_message(session.verdict()));
      report_failure(image_verdict_message(session.verdict()));
      return PullResult::ImageRejected;
    }
    if (written != ESP_OK) {
      report_failure("Could not write to flash");
      return PullResult::WriteFailed;
    }
  }

  const esp_err_t finished = session.finish();
  if (finished != ESP_OK) {
    ESP_LOGE(kTag, "downloaded image failed finalisation: %s",
             esp_err_to_name(finished));
    report_failure(finished == ESP_ERR_INVALID_SIZE
                       ? "Download ended before a complete header"
                       : "Image failed its own integrity check");
    return PullResult::ImageRejected;
  }
  return PullResult::Armed;
}

const char* pull_result_message(PullResult result) {
  switch (result) {
    case PullResult::Armed:
      return "Firmware armed; restart to verify";
    case PullResult::InsecureUrl:
      return "Firmware URL must be https";
    case PullResult::TransportFailed:
      return "Could not download the firmware";
    case PullResult::ImageRejected:
      return "Downloaded file is not valid firmware";
    case PullResult::WriteFailed:
      return "Could not write the firmware to flash";
  }
  return "Update failed";
}

}  // namespace ota
