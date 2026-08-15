#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_err.h>
#include <esp_ota_ops.h>

#include "app_snapshot.hpp"
#include "ota_image.hpp"
#include "ota_prefix.hpp"

namespace ota {

// Set once at startup, to wifi_provision::set_ota. A function pointer rather
// than a direct call because wifi_provision owns the one AppSnapshot and
// depends on ui; having the ota component call into it would invert that and
// make the dependency circular. Same shape as
// ui::set_setup_gesture_handler.
void set_progress_handler(void (*handler)(const app_core::OtaData&));

// Publishes through that handler. Exposed so the feeders can report their own
// failures - a download that never reached Session still needs to say so on
// the panel. A no-op when no handler is set.
void publish_progress(const app_core::OtaData& data);

// The single write path both feeders share: a browser upload streaming a
// request body, and a URL pull streaming an HTTP response. Neither knows
// anything about partitions or validation; they only supply bytes.
//
// Not thread-safe and not meant to be: one update at a time, owned by the task
// feeding it.
class Session {
 public:
  // total_bytes 0 means the feeder never learned the size (no Content-Length),
  // in which case the panel shows WORKING instead of inventing a percentage.
  explicit Session(std::size_t total_bytes);
  ~Session();

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Feeds one chunk. The slot is not erased and esp_ota_begin is not called
  // until enough bytes have arrived to judge the image, so a wrong file costs
  // nothing. Returns ESP_ERR_INVALID_VERSION when the image is rejected -
  // check verdict() for which rejection.
  esp_err_t write(const uint8_t* data, std::size_t length);

  // Finalises and points the bootloader at the new slot. The caller reboots.
  // Fails if the image was never judged, i.e. the upload ended inside the
  // header.
  esp_err_t finish();

  // Safe at any point, including before the slot was ever opened. Called by
  // the destructor, so an abandoned upload cannot leave a half-open handle.
  void abort();

  ImageVerdict verdict() const { return prefix_.verdict(); }
  const ImageInfo& image() const { return prefix_.info(); }
  std::size_t written() const { return written_; }

 private:
  esp_err_t open_slot();
  esp_err_t write_to_flash(const uint8_t* data, std::size_t length);
  void publish(app_core::OtaPhase phase);

  PrefixInspector prefix_;
  esp_ota_handle_t handle_ = 0;
  const esp_partition_t* partition_ = nullptr;
  std::size_t total_ = 0;
  std::size_t written_ = 0;
  // Last percentage actually published. Republishing an unchanged figure would
  // rebuild the OTA page on every chunk, and on a reflective panel a full
  // repaint is visible.
  int published_percent_ = -1;
  bool open_ = false;
  bool finished_ = false;
};

}  // namespace ota
