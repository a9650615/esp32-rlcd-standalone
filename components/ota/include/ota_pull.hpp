#pragma once

#include <cstdint>
#include <string>

namespace ota {

enum class PullResult : uint8_t {
  // Written and the bootloader is pointed at it. The caller reboots; the
  // rollback guard decides whether it stays.
  Armed,
  InsecureUrl,
  TransportFailed,
  ImageRejected,
  WriteFailed,
};

// Downloads firmware over HTTPS and feeds it through the same ota::Session the
// browser upload uses, so both feeders get the same header validation, the
// same erase-only-once-judged ordering, and the same on-screen progress. This
// is why it is built on esp_http_client rather than esp_https_ota: the latter
// runs its own private begin/write/end and leaves no way to interpose the
// project's own checks.
//
// Blocking, and it holds a 4 KiB buffer plus a TLS session - call it from a
// task with room for both, never from the LVGL thread.
//
// Does not reboot. The caller decides when, so a device mid-update is not
// restarted out from under whoever triggered it.
PullResult pull_from_url(const std::string& url);

// ASCII, for the panel and any HTTP response.
const char* pull_result_message(PullResult result);

}  // namespace ota
