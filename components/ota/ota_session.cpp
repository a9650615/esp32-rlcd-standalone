#include "ota_session.hpp"

#include "ota_quiesce.hpp"

#include <esp_log.h>
#include <esp_partition.h>

namespace ota {
namespace {

constexpr char kTag[] = "ota";

void (*g_progress_handler)(const app_core::OtaData&) = nullptr;

}  // namespace

void set_progress_handler(void (*handler)(const app_core::OtaData&)) {
  g_progress_handler = handler;
}

void publish_progress(const app_core::OtaData& data) {
  if (g_progress_handler != nullptr) g_progress_handler(data);
}

Session::Session(std::size_t total_bytes) : total_(total_bytes) {}

Session::~Session() { abort(); }

void Session::publish(app_core::OtaPhase phase) {
  if (g_progress_handler == nullptr) return;
  app_core::OtaData data;
  data.phase = phase;
  if (total_ > 0) {
    data.percent_known = true;
    const std::size_t percent = written_ * 100 / total_;
    data.percent = static_cast<uint8_t>(percent > 100 ? 100 : percent);
  }
  g_progress_handler(data);
}

esp_err_t Session::open_slot() {
  partition_ = esp_ota_get_next_update_partition(nullptr);
  if (partition_ == nullptr) {
    ESP_LOGE(kTag, "no OTA slot available to write into");
    return ESP_ERR_NOT_FOUND;
  }
  // Before the first erase, and on this task, so a module can put its
  // hardware somewhere safe. A push sent during playback produced loud noise
  // from the speaker and then failed; the amplifier was left enabled with an
  // undriven input. See ota_quiesce.hpp and
  // docs/design/2026-08-20-quiescing-modules-before-an-ota-write.md.
  //
  // Here rather than in the HTTP handler: every path that writes firmware
  // goes through open_slot(), so putting it at the point of the erase means a
  // future feeder cannot forget to call it.
  run_quiesce_hooks();

  // OTA_SIZE_UNKNOWN erases the whole partition, which is the only correct
  // choice when the feeder had no Content-Length to pass on.
  const std::size_t erase_size =
      total_ > 0 ? total_ : static_cast<std::size_t>(OTA_SIZE_UNKNOWN);
  const esp_err_t result = esp_ota_begin(partition_, erase_size, &handle_);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_begin failed: %s", esp_err_to_name(result));
    return result;
  }
  open_ = true;
  ESP_LOGI(kTag, "writing %s image into slot %s (%u bytes expected)",
           prefix_.info().version.c_str(), partition_->label,
           static_cast<unsigned>(total_));
  return ESP_OK;
}

esp_err_t Session::write_to_flash(const uint8_t* data, std::size_t length) {
  if (length == 0) return ESP_OK;
  const esp_err_t result = esp_ota_write(handle_, data, length);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_write failed at %u bytes: %s",
             static_cast<unsigned>(written_), esp_err_to_name(result));
    return result;
  }
  written_ += length;
  return ESP_OK;
}

esp_err_t Session::write(const uint8_t* data, std::size_t length) {
  if (finished_) return ESP_ERR_INVALID_STATE;
  if (data == nullptr || length == 0) return ESP_OK;

  if (!prefix_.ready()) {
    prefix_.feed(data, length);
    if (!prefix_.ready()) {
      // Still short of a verdict. Nothing has been erased, and the bytes are
      // held in the inspector rather than dropped.
      return ESP_OK;
    }
    if (prefix_.verdict() != ImageVerdict::Ok) {
      ESP_LOGE(kTag, "rejecting upload: %s (project=%s version=%s)",
               image_verdict_message(prefix_.verdict()),
               prefix_.info().project_name.c_str(),
               prefix_.info().version.c_str());
      return ESP_ERR_INVALID_VERSION;
    }

    const esp_err_t opened = open_slot();
    if (opened != ESP_OK) return opened;

    // The inspected bytes are image bytes too. Write the held-back prefix
    // first, then only the part of this chunk that was not absorbed into it -
    // writing the whole chunk here would duplicate the overlap and corrupt
    // an image that would then pass every later check.
    const esp_err_t prefix_written =
        write_to_flash(prefix_.buffer(), kImagePrefixBytes);
    if (prefix_written != ESP_OK) return prefix_written;

    const std::size_t consumed = prefix_.consumed_from_last_chunk();
    data += consumed;
    length -= consumed;
  }

  if (!open_) return ESP_ERR_INVALID_STATE;
  const esp_err_t result = write_to_flash(data, length);
  if (result != ESP_OK) return result;

  if (total_ > 0) {
    const int percent = static_cast<int>(written_ * 100 / total_);
    if (percent != published_percent_) {
      published_percent_ = percent;
      publish(app_core::OtaPhase::Receiving);
    }
  } else if (published_percent_ < 0) {
    // Unknown size: one publish to get the page up, then nothing further to
    // say until the phase changes.
    published_percent_ = 0;
    publish(app_core::OtaPhase::Receiving);
  }
  return ESP_OK;
}

esp_err_t Session::finish() {
  if (finished_) return ESP_ERR_INVALID_STATE;
  if (!open_) {
    // The upload ended before the image could even be judged.
    ESP_LOGE(kTag, "upload ended after %u bytes, before a full header",
             static_cast<unsigned>(prefix_.buffered()));
    return ESP_ERR_INVALID_SIZE;
  }

  publish(app_core::OtaPhase::Writing);
  finished_ = true;
  open_ = false;

  esp_err_t result = esp_ota_end(handle_);
  handle_ = 0;
  if (result != ESP_OK) {
    // ESP_ERR_OTA_VALIDATE_FAILED here means the image's own hash did not
    // check out - a truncated or corrupted transfer that the header could not
    // have caught.
    ESP_LOGE(kTag, "esp_ota_end rejected the image: %s",
             esp_err_to_name(result));
    return result;
  }

  result = esp_ota_set_boot_partition(partition_);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "esp_ota_set_boot_partition failed: %s",
             esp_err_to_name(result));
    return result;
  }
  ESP_LOGW(kTag, "slot %s armed with %u bytes; reboot to verify",
           partition_->label, static_cast<unsigned>(written_));
  return ESP_OK;
}

void Session::abort() {
  if (!open_) return;
  open_ = false;
  ESP_LOGW(kTag, "update aborted after %u bytes",
           static_cast<unsigned>(written_));
  // esp_ota_abort releases the handle without arming the slot. The partition
  // is left partially erased, which is harmless: it is not the running slot,
  // and the next update erases it again before writing.
  (void)esp_ota_abort(handle_);
  handle_ = 0;
}

}  // namespace ota
