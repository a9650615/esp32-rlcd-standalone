#include "ota_pull.hpp"

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

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

// Opens the URL, following redirects, and leaves the client positioned to read
// the body. Returns the final status code, or a negative value if the request
// could not be made at all.
//
// esp_http_client only follows redirects inside esp_http_client_perform(); the
// open/fetch_headers/read path this file needs in order to stream into flash
// never reaches that code, so a 302 arrives as the response. Every GitHub
// release download is a 302 to release-assets.githubusercontent.com, so
// without this the one case the feature exists for is the one that fails -
// reported, confusingly, as "the host returned an error".
//
// esp_http_client_set_redirection is IDF's own function for this, the same one
// perform() calls, so the URL rewriting is not reimplemented here.
int open_following_redirects(esp_http_client_handle_t client) {
  constexpr int kMaxRedirects = 5;
  for (int attempt = 0; attempt <= kMaxRedirects; ++attempt) {
    if (esp_http_client_open(client, 0) != ESP_OK) return -1;
    esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    const bool redirect = status == 301 || status == 302 || status == 303 ||
                          status == 307 || status == 308;
    if (!redirect) return status;
    if (esp_http_client_set_redirection(client) != ESP_OK) return status;
    // The body of the redirect response has to be drained before the socket
    // can carry the next request.
    esp_http_client_close(client);
    ESP_LOGI(kTag, "following HTTP %d redirect", status);
  }
  ESP_LOGE(kTag, "too many redirects");
  return -1;
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

  const int status = open_following_redirects(client);
  if (status < 0) {
    report_failure("Could not reach the firmware host");
    return PullResult::TransportFailed;
  }
  if (status != 200) {
    ESP_LOGE(kTag, "firmware host answered HTTP %d", status);
    report_failure("Firmware host returned an error");
    return PullResult::TransportFailed;
  }
  const int64_t content_length = esp_http_client_get_content_length(client);

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

namespace {

// Genuinely writes flash (pull_from_url() feeds ota::Session, which calls
// esp_ota_write()), so unlike modules/audio's tone/sweep tasks and
// main/app_main.cpp's update_check_task, this one's stack must stay in
// internal RAM - a task whose stack lives in PSRAM must never run while the
// flash cache is disabled, which is exactly the window a flash write opens
// (see freertos/Kconfig's FREERTOS_TASK_CREATE_ALLOW_EXT_MEM help text).
// Nothing here moves to PSRAM.
//
// One at a time, refused rather than allowed to race: without this, the
// settings row's install action and a POST /ota-url arriving over the LAN
// at the same moment could each start a pull_task, and two ota::Session
// instances would then call esp_ota_begin()/esp_ota_write() against the
// same partition concurrently - a genuine latent defect, found while this
// module was being changed for an unrelated reason, not something the two
// concurrent-pull case had ever been exercised against. Same non-blocking
// try-lock shape as modules/audio's g_playback_busy, for the same reason:
// refuse cleanly and immediately rather than block the caller (the LVGL
// thread, or an HTTP handler) until a slot frees up.
SemaphoreHandle_t g_pull_busy = nullptr;

// Lazily created on first use, same reasoning as modules/audio's
// claim_playback_slot(): nothing needs this before the first pull request.
bool claim_pull_slot() {
  if (g_pull_busy == nullptr) {
    g_pull_busy = xSemaphoreCreateBinary();
    if (g_pull_busy == nullptr) return false;
    xSemaphoreGive(g_pull_busy);  // seed with one token: "available"
  }
  return xSemaphoreTake(g_pull_busy, 0) == pdTRUE;
}

void pull_task(void* argument) {
  // Owns the copy the caller handed over, so the URL cannot go out of scope
  // while the download is still running.
  std::unique_ptr<std::string> url(static_cast<std::string*>(argument));
  const PullResult result = pull_from_url(*url);
  if (result == PullResult::Armed) {
    ESP_LOGW(kTag, "pulled firmware armed; restarting");
    esp_restart();  // Does not return; nothing left to release.
  }
  // pull_from_url already put the reason on the panel. Nothing to reboot for;
  // the running image is untouched and the carousel simply carries on.
  ESP_LOGE(kTag, "firmware pull failed: %s", pull_result_message(result));
  xSemaphoreGive(g_pull_busy);
  vTaskDelete(nullptr);
}

}  // namespace

bool start_pull(const std::string& url) {
  if (!claim_pull_slot()) {
    ESP_LOGW(kTag, "firmware pull refused: a pull is already in progress");
    return false;
  }
  auto owned = std::make_unique<std::string>(url);
  // 16384 B: 4096 would not survive the TLS handshake. weather_monitor_task
  // needed this much for the handshake alone, and this additionally holds a
  // 4 KiB read buffer. Internal RAM - see pull_task's own comment for why
  // this one cannot move to PSRAM the way the other two on-demand tasks in
  // this same pass did.
  if (xTaskCreate(&pull_task, "ota_pull", 16384, owned.get(),
                  tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
    ESP_LOGE(kTag,
             "firmware pull task creation failed: free internal=%u largest "
             "block=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    xSemaphoreGive(g_pull_busy);
    return false;
  }
  // The task owns it now.
  (void)owned.release();
  return true;
}

}  // namespace ota
