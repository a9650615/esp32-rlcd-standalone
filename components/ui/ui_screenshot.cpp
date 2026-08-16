#ifndef NDEBUG
#include "ui_screenshot.hpp"

#include <array>
#include <cstdio>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>

#include "lvgl_port.hpp"

namespace ui {
namespace {

constexpr char kTag[] = "ui_shot";

// One per PageId. Each page dumps once per boot: enough to look at every
// screen after a flash, and quiet afterwards. A reflash - which is what
// follows any layout change worth looking at - resets them.
std::array<bool, 8> g_sent{};

// Serial carries this a line at a time so a capture stays readable and a
// dropped line is visible as a short frame rather than a corrupt one.
constexpr size_t kLineBytes = 96;

}  // namespace

void dump_frame_once(app_core::PageId page, const char* name) {
  const auto index = static_cast<size_t>(page);
  if (index >= g_sent.size() || g_sent[index]) return;

  static std::array<uint8_t, board::kFramebufferSnapshotBytes> raw;
  if (!board::framebuffer_snapshot(raw.data(), raw.size())) return;
  g_sent[index] = true;

  ESP_LOGW(kTag, "SHOT BEGIN %s 400x300", name);
  size_t offset = 0;
  while (offset < raw.size()) {
    const size_t chunk = std::min(kLineBytes, raw.size() - offset);
    unsigned char encoded[kLineBytes * 4 / 3 + 8];
    size_t written = 0;
    if (mbedtls_base64_encode(encoded, sizeof(encoded), &written,
                              raw.data() + offset, chunk) != 0) {
      ESP_LOGE(kTag, "SHOT ABORT encode failed at %u", (unsigned)offset);
      return;
    }
    encoded[written] = '\0';
    // ESP_LOG, not printf. printf goes to serial only; net_log mirrors the log
    // sink, so a frame emitted with printf is unreachable the moment the USB
    // cable comes off - which is exactly when a picture of the panel is the
    // only way to see it. decode-screenshots.py searches rather than anchors,
    // so the tag and timestamp in front of the payload cost nothing.
    ESP_LOGW(kTag, "SHOT %s", reinterpret_cast<char*>(encoded));
    // net_log takes its ring mutex with a zero-tick try-lock, so a log line
    // that arrives while the sender task holds it is dropped rather than
    // blocking the caller - the right trade for logging in general, and fatal
    // for the one caller that emits 157 lines back to back. Over the network
    // that cost exactly one line of a frame, which the decoder correctly
    // refused to turn into a picture. Yielding lets the sender drain.
    //
    // This stalls the LVGL thread for roughly a third of a second. It is a
    // debug build only, once per page per boot, and the alternative is a
    // screenshot tool that silently returns four pages out of five.
    vTaskDelay(pdMS_TO_TICKS(2));
    offset += chunk;
  }
  ESP_LOGW(kTag, "SHOT END %s", name);
}

}  // namespace ui
#endif
