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

// Runs pull_from_url on its own task and restarts the board if it arms a slot.
//
// Every caller needs exactly this and none of them can do it inline: the HTTP
// handler would stall the server for the length of the download (the phone
// times out and retries, starting a second update on top of the first), and
// the settings row runs on the LVGL thread, which would freeze the panel and
// trip the watchdog. Sharing it also means the settings row and the web page
// cannot drift into two different notions of what installing is.
//
// Returns false only if the task could not be created; the outcome of the
// download itself arrives on the panel through ota::Session.
bool start_pull(const std::string& url);

}  // namespace ota
